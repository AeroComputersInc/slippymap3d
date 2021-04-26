/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Christoph Brill
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sstream>
#include <boost/filesystem.hpp>
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
#include <boost/thread.hpp>
#include <boost/asio/io_service.hpp>

#include "loader.h"
#include "global.h"

boost::thread_group pool;
boost::asio::io_service ioService;
boost::asio::io_service::work * work;

Loader* Loader::_instance = nullptr;

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written;
    written = fwrite(ptr, size, nmemb, stream);
    return written;
}

Loader::Loader() {
    std::cout << "Loader ..." << std::endl;

    work = new boost::asio::io_service::work(ioService);
    for (int i = 0; i < 5; i++) {
        pool.create_thread(boost::bind(&boost::asio::io_service::run, &ioService));
    }
}

Loader::~Loader() {
    std::cout << "Destroy Loader ..." << std::endl;

    ioService.stop();
    pool.join_all();
    delete work;
}

void Loader::download_image(Tile* tile) {
    std::cout << "Download image CURL" << std::endl;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        std::cerr << "Failed to initialize curl" << std::endl;
        return;
    }

    std::stringstream dirname;
    dirname << TILE_DIR << tile->zoom << "/" << tile->x;
    std::string dir = dirname.str();
    boost::filesystem::create_directories(dir);
    std::string filename = tile->get_filename();
//     std::string url = "http://localhost/osm_tiles/" + filename;
//    std::string url = "https://tiles.wmflabs.org/hillshading/" + filename;
    std::string url = "https://tiles.wmflabs.org/osm-no-labels/" + filename;
    std::cout << "Downloading texture from url " << url << std::endl;

    std::string file = TILE_DIR + filename;
    FILE* fp = fopen(file.c_str(), "wb");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        std::cout << "Failed to download: " << url << " " << res << std::endl;
    } else {
        std::cout << "CURL download success: " << res << std::endl;
        tile->texid = 0;
    }

}

void Loader::load_image(Tile& tile) {
    std::cout << "Load image ..." << std::endl;
    std::string filename = TILE_DIR + tile.get_filename();
    if (!boost::filesystem::exists(filename)) {
        std::cout << "Download image: " << filename << std::endl;
        ioService.post(boost::bind(&Loader::download_image, this, &tile));
        return;
    } else {
        std::cout << "image exist " << filename << " size: " << boost::filesystem::file_size(filename) << std::endl;
        if (boost::filesystem::file_size(filename) < 15) {
            std::cout << "Replace Empty Image: " << filename << std::endl;
            boost::filesystem::remove(filename);
            ioService.post(boost::bind(&Loader::download_image, this, &tile));
            return;
        } else {
            std::cout << "File Cached: " << filename << std::endl;
        }
    }
    open_image(tile);
}

void Loader::open_image(Tile &tile) {
    std::cout << "Open image ..." << std::endl;
    std::string filename = TILE_DIR + tile.get_filename();
    SDL_Surface *texture = IMG_Load(filename.c_str());

    char tmp[4096];
    getcwd(tmp, 4096);
    std::cout << "Loading texture " << filename << " from directory " << tmp << std::endl;

    if (texture) {
        if (SDL_MUSTLOCK(texture)) {
            SDL_LockSurface(texture);
        }

        GLenum texture_format;
        if (texture->format->BytesPerPixel == 4) {
            if (texture->format->Rmask == 0x000000ff) {
                texture_format = GL_RGBA;
            } else {
                texture_format = GL_BGRA;
            }
        } else if (texture->format->BytesPerPixel == 3) {
            if (texture->format->Rmask == 0x000000ff) {
                texture_format = GL_RGB;
            } else {
                texture_format = GL_BGR;
            }
        } else {
            std::cout << "INVALID (" << SDL_GetPixelFormatName(texture->format->format);
            SDL_PixelFormat* format = SDL_AllocFormat(SDL_PIXELFORMAT_BGR24);
            SDL_Surface* tmp = SDL_ConvertSurface(texture, format, 0);
            texture_format = GL_BGR;
            if (SDL_MUSTLOCK(texture)) {
                SDL_UnlockSurface(texture);
            }
            SDL_FreeSurface(texture);
            texture = tmp;
            if (SDL_MUSTLOCK(texture)) {
                SDL_LockSurface(texture);
            }
            std::cout << " -> " << SDL_GetPixelFormatName(texture->format->format) << ") ";
        }

        GLuint texid;
        glGenTextures(1, &texid);
        glBindTexture(GL_TEXTURE_2D, texid);

        glTexImage2D(GL_TEXTURE_2D, 0, 3, texture->w, texture->h, 0, texture_format, GL_UNSIGNED_BYTE, texture->pixels);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (SDL_MUSTLOCK(texture)) {
            SDL_UnlockSurface(texture);
        }
        SDL_FreeSurface(texture);

        tile.texid = texid;

        std::cout << "Load Texture -> SUCCESS" << std::endl;
    } else {
        tile.texid = TileFactory::instance()->get_dummy();
        std::cout << "Load Texture -> FAILED" << std::endl;
    }

}
