#pragma once

#include <boost/filesystem.hpp>
#include <boost/asio/ssl/context.hpp>

namespace arqma {

void generate_dh_pem(const char* dh_path);
void generate_cert(const char* cert_path, const char* key_path);
void load_server_certificate(const boost::filesystem::path& base_path, boost::asio::ssl::context& ctx);
}