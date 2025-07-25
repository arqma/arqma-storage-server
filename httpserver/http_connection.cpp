#include "http_connection.h"
#include "Database.hpp"
#include "Item.hpp"
#include "channel_encryption.hpp"
#include "dev_sink.h"
#include "net_stats.h"
#include "rate_limiter.h"
#include "security.h"
#include "serialization.h"
#include "server_certificates.h"
#include "service_node.h"
#include "signature.h"
#include "utils.hpp"
#include "https_client.h"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <openssl/sha.h>
#include <sodium.h>
#include <sstream>
#include <string>
#include <thread>

using json = nlohmann::json;
using namespace std::chrono_literals;

using tcp = boost::asio::ip::tcp;    // from <boost/asio.hpp>
namespace http = boost::beast::http; // from <boost/beast/http.hpp>
namespace sp = std::placeholders; // for std::placeholders:: code shorten

/// +===========================================

static constexpr auto ARQMA_EPHEMKEY_HEADER = "X-Arqma-EphemKey";
static constexpr auto ARQMA_LONG_POLL_HEADER = "X-Arqma-Long-Poll";
static constexpr auto ARQMA_FILE_SERVER_TARGET_HEADER = "X-Arqma-File-Server-Target";
static constexpr auto ARQMA_FILE_SERVER_VERB_HEADER = "X-Arqma-File-Server-Verb";
static constexpr auto ARQMA_FILE_SERVER_HEADERS_HEADER = "X-Arqma-File-Server-Headers";

using arqma::storage::Item;

using error_code = boost::system::error_code;

namespace arqma {

constexpr auto TEST_RETRY_PERIOD = std::chrono::milliseconds(50);

// Note: on the client side the limit is different
// as it is not encrypted/encoded there yet.
// The choice is somewhat arbitrary but it roughly
// corresponds to the client-side limit of 2000 chars
// of unencrypted message body in our experiments
// (rounded up)
constexpr size_t MAX_MESSAGE_BODY = 102400; // 100 kB

std::shared_ptr<request_t> build_post_request(const char* target, std::string&& data)
{
  auto req = std::make_shared<request_t>();
  req->body() = std::move(data);
  req->method(http::verb::post);
  req->set(http::field::host, "service node");
  req->target(target);
  req->prepare_payload();
  return req;
}

void make_http_request(boost::asio::io_context& ioc,
                       const std::string& sn_address, uint16_t port,
                       const std::shared_ptr<request_t>& req,
                       http_callback_t&& cb) {

    error_code ec;
    tcp::endpoint endpoint;
    tcp::resolver resolver(ioc);
    tcp::resolver::iterator destination = resolver.resolve(sn_address, "http", ec);
    if (ec) {
        ARQMA_LOG(error,
                  "http: Failed to parse the IP address <{}>. Error code = {}. "
                  "Message: {}",
                  sn_address, ec.value(), ec.message());
        return;
    }
    while (destination != tcp::resolver::iterator()) {
        const tcp::endpoint thisEndpoint = (destination++)->endpoint();
        if (!thisEndpoint.address().is_v4()) {
            continue;
        }
        endpoint = thisEndpoint;
    }
    endpoint.port(port);

    auto session =
        std::make_shared<HttpClientSession>(ioc, endpoint, req, std::move(cb));

    session->start();
}
// ======================== Arqmad Client ========================
ArqmadClient::ArqmadClient(boost::asio::io_context& ioc, std::string ip, uint16_t port)
    : ioc_(ioc), arqmad_rpc_ip_(std::move(ip)), arqmad_rpc_port_(port) {}

void ArqmadClient::make_arqmad_request(boost::string_view method, const nlohmann::json& params, http_callback_t&& cb) const
{
  make_custom_arqmad_request(arqmad_rpc_ip_, arqmad_rpc_port_, method, params, std::move(cb));
}

void ArqmadClient::make_custom_arqmad_request(const std::string& daemon_ip, const uint16_t daemon_port, boost::string_view method,
                                              const nlohmann::json& params, http_callback_t&& cb) const
{
    auto req = std::make_shared<request_t>();

    const std::string target = "/json_rpc";

    nlohmann::json req_body;
    req_body["jsonrpc"] = "2.0";
    req_body["id"] = "0";
    req_body["method"] = method;
    req_body["params"] = params;

    req->body() = req_body.dump();
    req->method(http::verb::post);
    req->target(target);
    req->prepare_payload();

    ARQMA_LOG(trace, "Making arqmad request, method: {}", method.to_string());

    make_http_request(ioc_, daemon_ip, daemon_port, req, std::move(cb));
}

static bool validateHexKey(const std::string& key, const size_t key_length = arqma::KEY_LENGTH) {
  return key.size() == 2 * key_length &&
         std::all_of(key.begin(), key.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

std::tuple<private_key_t, private_key_ed25519_t, private_key_t>
ArqmadClient::wait_for_privkey()
{
  // fetch Service-Node private key from Arqmad. Do this synchronously
  // because we can't finish startup until we have it.
  arqma::private_key_t private_key;
  arqma::private_key_ed25519_t private_key_ed;
  arqma::private_key_t private_key_x;
  ARQMA_LOG(info, "Retrieving Service-Node key from Arqmad");
  boost::asio::steady_timer delay{ioc_};
  std::function<void(arqma::sn_response_t && res)> key_fetch;
  key_fetch = [&](arqma::sn_response_t res) {
    try {
      if (res.error_code != arqma::SNodeError::NO_ERROR)
        throw std::runtime_error(arqma::error_string(res.error_code));
      else if (!res.body)
        throw std::runtime_error("empty body");
      else {
        auto r = nlohmann::json::parse(*res.body);
        const auto &legacy_privkey = r.at("result").at("service_node_privkey").get_ref<const std::string &>();
        const auto &privkey_ed = r.at("result").at("service_node_ed25519_privkey").get_ref<const std::string &>();
        const auto &privkey_x = r.at("result").at("service_node_x25519_privkey").get_ref<const std::string &>();
        if (!validateHexKey(legacy_privkey) || !validateHexKey(privkey_ed, private_key_ed25519_t::LENGTH) || !validateHexKey(privkey_x))
          throw std::runtime_error("returned value is not Hex");
        else {
          private_key = arqma::arqmadKeyFromHex(legacy_privkey);
          private_key_ed = private_key_ed25519_t::from_hex(privkey_ed);
          private_key_x = arqma::arqmadKeyFromHex(privkey_x);
        }
      }
    } catch (const std::exception &e) {
      ARQMA_LOG(critical, "Error retrieving Service-Node private key from arqmad @ {}:{}: {}. Is arqmad running? Retrying in 5s",
                arqmad_rpc_ip_, arqmad_rpc_port_, e.what());

      delay.expires_after(std::chrono::seconds{5});
      delay.async_wait([this, &key_fetch](const boost::system::error_code &) {
        make_arqmad_request("get_service_node_privkey", {}, key_fetch); });
    }
  };
  make_arqmad_request("get_service_node_privkey", {}, key_fetch);
  ioc_.run();
  ioc_.restart();

  return std::tuple<private_key_t, private_key_ed25519_t, private_key_t>{private_key, private_key_ed, private_key_x};
}
// =============================================================
namespace http_server {

// "Loop" forever accepting new connections.
static void accept_connection(boost::asio::io_context& ioc, boost::asio::ssl::context& ssl_ctx, tcp::acceptor& acceptor, ServiceNode& sn,
                              ChannelEncryption<std::string>& channel_encryption, RateLimiter& rate_limiter, const Security& security)
{
  static boost::asio::steady_timer acceptor_timer(ioc);
  constexpr std::chrono::milliseconds ACCEPT_DELAY = 50ms;

  acceptor.async_accept([&](const error_code& ec, tcp::socket socket) {
    ARQMA_LOG(trace, "connection accepted");
    if (!ec) {
      std::make_shared<connection_t>(ioc, ssl_ctx, std::move(socket), sn, channel_encryption, rate_limiter, security)->start();
      accept_connection(ioc, ssl_ctx, acceptor, sn, channel_encryption, rate_limiter, security);
    } else {
      if (ec == boost::system::errc::too_many_files_open) {
        ARQMA_LOG(critical, "Too many open files, aborting");
        abort();
      }
      ARQMA_LOG(error, "Could not accept a new connection {}: {}. Will only start accepting new connections after a short delay.",
                ec.value(), ec.message());

      acceptor_timer.expires_after(ACCEPT_DELAY);
      acceptor_timer.async_wait([&](const error_code& ec) {
        if (ec && ec != boost::asio::error::operation_aborted) {
          abort();
        }

        accept_connection(ioc, ssl_ctx, acceptor, sn, channel_encryption, rate_limiter, security);
      });
    }
  });
}

void run(boost::asio::io_context& ioc, const std::string& ip, uint16_t port,
         const boost::filesystem::path& base_path, ServiceNode& sn,
         ChannelEncryption<std::string>& channel_encryption,
         RateLimiter& rate_limiter, Security& security) {

    ARQMA_LOG(trace, "http server run");

    const auto address =
        boost::asio::ip::make_address(ip); /// throws if incorrect

    tcp::acceptor acceptor{ioc, {address, port}};

    ssl::context ssl_ctx{ssl::context::tlsv12};

    load_server_certificate(base_path, ssl_ctx);

    security.generate_cert_signature();

    accept_connection(ioc, ssl_ctx, acceptor, sn, channel_encryption,
                      rate_limiter, security);

    ioc.run();
}

/// ============ connection_t ============

connection_t::connection_t(boost::asio::io_context& ioc, ssl::context& ssl_ctx,
                           tcp::socket socket, ServiceNode& sn,
                           ChannelEncryption<std::string>& channel_encryption,
                           RateLimiter& rate_limiter, const Security& security)
    : ioc_(ioc), ssl_ctx_(ssl_ctx), socket_(std::move(socket)),
      stream_(socket_, ssl_ctx_), service_node_(sn),
      channel_cipher_(channel_encryption), rate_limiter_(rate_limiter),
      repeat_timer_(ioc), deadline_(ioc, SESSION_TIME_LIMIT),
      notification_ctx_{boost::none}, security_(security) {

    static uint64_t instance_counter = 0;
    conn_idx = instance_counter++;

    get_net_stats().connections_in++;

    ARQMA_LOG(trace, "connection_t [{}]", conn_idx);

    request_.body_limit(1024 * 1024 * 10); // 10 MB
    std::cerr << request_.get() << std::endl;

    start_timestamp_ = std::chrono::steady_clock::now();
}

connection_t::~connection_t() {

    // Safety net
    if (stream_.lowest_layer().is_open()) {
        ARQMA_LOG(debug, "Client socket should be closed by this point, but "
                         "wasn't. Closing now.");
        stream_.lowest_layer().close();
    }

    get_net_stats().connections_in--;

    ARQMA_LOG(trace, "~connection_t [{}]", conn_idx);
}

void connection_t::start() {
    register_deadline();
    do_handshake();
}

void connection_t::do_handshake() {
    // Perform the SSL handshake
    stream_.async_handshake(ssl::stream_base::server,
                            std::bind(&connection_t::on_handshake,
                                      shared_from_this(),
                                      sp::_1));
}

void connection_t::on_handshake(boost::system::error_code ec)
{
  const auto sockfd = stream_.lowest_layer().native_handle();
  ARQMA_LOG(debug, "Open https socket: {}", sockfd);
  get_net_stats().record_socket_open(sockfd);
  if (ec) {
    ARQMA_LOG(warn, "ssl handshake failed: ec: {} ({})", ec.value(), ec.message());
    this->clean_up();
    deadline_.cancel();
    return;
  }

  read_request();
}

void connection_t::clean_up()
{
  this->do_close();

  if (this->notification_ctx_)
    this->service_node_.remove_listener(this->notification_ctx_->pubkey, this);
}

void connection_t::notify(boost::optional<const message_t&> msg) {

    if (!notification_ctx_) {
        ARQMA_LOG(error,
                  "Trying to notify a connection without notification context");
        return;
    }

    if (msg) {
        ARQMA_LOG(trace, "Processing message notification: {}", msg->data);
        // save messages, so we can access them once the timer event happens
        notification_ctx_->message = msg;
    }
    // the timer callback will be called once we complete the current callback
    notification_ctx_->timer.cancel();
}

// Asynchronously receive a complete request message.
void connection_t::read_request() {

    auto on_data = [self = shared_from_this()](error_code ec,
                                               size_t bytes_transferred) {
        ARQMA_LOG(trace, "on data: {} bytes", bytes_transferred);

        if (ec) {
            ARQMA_LOG(
                error,
                "Failed to read from a socket [{}: {}], connection idx: {}",
                ec.value(), ec.message(), self->conn_idx);
            self->clean_up();
            self->deadline_.cancel();
            return;
        }

        // NOTE: this is blocking, we should make this asynchronous
        try {
            self->process_request();
        } catch (const std::exception& e) {
            ARQMA_LOG(critical, "Exception caught processing a request: {}",
                      e.what());
            self->body_stream_ << e.what();
        }

        if (!self->delay_response_) {
            self->write_response();
        }
    };

    http::async_read(stream_, buffer_, request_, on_data);
}

bool connection_t::validate_snode_request() {
    if (!parse_header(ARQMA_SENDER_SNODE_PUBKEY_HEADER,
                      ARQMA_SNODE_SIGNATURE_HEADER)) {
        ARQMA_LOG(debug,
                  "Missing signature headers for a Service Node request");
        return false;
    }
    const auto& signature = header_[ARQMA_SNODE_SIGNATURE_HEADER];
    const auto& public_key_b32z = header_[ARQMA_SENDER_SNODE_PUBKEY_HEADER];

    /// Known service node
    const std::string snode_address = public_key_b32z + ".snode";
    if (!service_node_.is_snode_address_known(snode_address)) {
        body_stream_ << "Unknown service node\n";
        ARQMA_LOG(debug, "Discarding signature from unknown service node: {}",
                  public_key_b32z);
        response_.result(http::status::unauthorized);
        return false;
    }

    if (!verify_signature(signature, public_key_b32z)) {
        constexpr auto msg = "Could not verify batch signature";
        ARQMA_LOG(debug, "{}", msg);
        body_stream_ << msg;
        response_.result(http::status::unauthorized);
        return false;
    }
    if (rate_limiter_.should_rate_limit(public_key_b32z)) {
        this->body_stream_ << "Too many requests\n";
        response_.result(http::status::too_many_requests);
        return false;
    }
    return true;
}

bool connection_t::verify_signature(const std::string& signature, const std::string& public_key_b32z)
{
  const auto body_hash = hash_data(request_.get().body());
  return check_signature(signature, body_hash, public_key_b32z);
}

void connection_t::process_storage_test_req(uint64_t height,
                                            const std::string& tester_pk,
                                            const std::string& msg_hash) {

    ARQMA_LOG(trace, "Performing storage test, attempt: {}", repetition_count_);

    std::string answer;

    const MessageTestStatus status = service_node_.process_storage_test_req(
        height, tester_pk, msg_hash, answer);
    const auto elapsed_time =
        std::chrono::steady_clock::now() - start_timestamp_;
    if (status == MessageTestStatus::SUCCESS) {
        ARQMA_LOG(
            debug, "Storage test success! Attempts: {}. Took {} ms",
            repetition_count_,
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time)
                .count());
        delay_response_ = true;

        nlohmann::json json_res;
        json_res["status"] = "OK";
        json_res["value"] = answer;

        this->body_stream_ << json_res.dump();

        response_.result(http::status::ok);
        this->write_response();
    } else if (status == MessageTestStatus::RETRY && elapsed_time < 1min) {
        delay_response_ = true;
        repetition_count_++;

        repeat_timer_.expires_after(TEST_RETRY_PERIOD);
        repeat_timer_.async_wait([self = shared_from_this(), height, msg_hash,
                                  tester_pk](const error_code& ec) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    ARQMA_LOG(error,
                              "Repeat timer failed for storage test [{}: {}]",
                              ec.value(), ec.message());
                }
            } else {
                self->process_storage_test_req(height, tester_pk, msg_hash);
            }
        });

    } else if (status == MessageTestStatus::WRONG_REQ) {
        nlohmann::json json_res;
        json_res["status"] = "wrong request";
        this->body_stream_ << json_res.dump();
        response_.result(http::status::ok);
    } else {
        ARQMA_LOG(error, "Failed storage test, tried {} times.",
                  repetition_count_);
        nlohmann::json json_res;
        json_res["status"] = "other";
        this->body_stream_ << json_res.dump();
        response_.result(http::status::ok);
    }
}

void connection_t::process_blockchain_test_req(uint64_t,
                                               const std::string& tester_pk,
                                               bc_test_params_t params) {
    ARQMA_LOG(debug, "Performing blockchain test");

    auto callback = [this](blockchain_test_answer_t answer) {
        this->response_.result(http::status::ok);

        nlohmann::json json_res;
        json_res["res_height"] = answer.res_height;

        this->body_stream_ << json_res.dump();
        this->write_response();
    };

    service_node_.perform_blockchain_test(params, std::move(callback));
}

static void print_headers(const request_t& req)
{
  ARQMA_LOG(info, "HEADERS:");
  for (const auto& field : req)
    ARQMA_LOG(info, "  [{}]: {}", field.name_string(), field.value());
}

void connection_t::process_proxy_req()
{
  ARQMA_LOG(debug, "Processing proxy request: we are first hop");

  const request_t& req = this->request_.get();
  delay_response_ = true;

  if (!parse_header(ARQMA_SENDER_KEY_HEADER, ARQMA_TARGET_SNODE_KEY))
  {
    ARQMA_LOG(debug, "Missing headers for a proxy request");
    return;
  }

  const auto& sender_key = header_[ARQMA_SENDER_KEY_HEADER];
  const auto& target_snode_key = header_[ARQMA_TARGET_SNODE_KEY];

  service_node_.process_proxy_req(req.body(), sender_key, target_snode_key, [this] (sn_response_t res)
  {
    if (res.raw_response)
      this->response_ = *res.raw_response;

    this->write_response();
  });
}

void connection_t::process_file_proxy_req()
{
  ARQMA_LOG(debug, "Processing file proxy request: we are the first hop");

  const request_t& original_req = this->request_.get();
  delay_response_ = true;

  if (!parse_header(ARQMA_FILE_SERVER_TARGET_HEADER, ARQMA_FILE_SERVER_VERB_HEADER, ARQMA_FILE_SERVER_HEADERS_HEADER))
  {
    ARQMA_LOG(error, "Missing headers for file proxy request");
    return;
  }

  const auto& target = header_[ARQMA_FILE_SERVER_TARGET_HEADER];
  const auto& verb_str = header_[ARQMA_FILE_SERVER_VERB_HEADER];
  const auto& headers_str = header_[ARQMA_FILE_SERVER_HEADERS_HEADER];

  ARQMA_LOG(trace, "Target: {}", target);
  ARQMA_LOG(trace, "Verb: {}", verb_str);
  ARQMA_LOG(trace, "Headers json: {}", headers_str);

  const json headers_json = json::parse(headers_str, nullptr, false);
  if (headers_json.is_discarded())
  {
    ARQMA_LOG(debug, "Bad file proxy request: invalid header json");
    response_.result(http::status::bad_request);
    return;
  }

  auto req = std::make_shared<request_t>();

  namespace http = boost::beast::http;

  if (verb_str == "POST") req->method(http::verb::post);
  else if (verb_str == "PATCH") req->method(http::verb::patch);
  else if (verb_str == "PUT") req->method(http::verb::put);
  else if (verb_str == "DELETE") req->method(http::verb::delete_);
  else req->method(http::verb::get);

  {
    const auto it = original_req.find(http::field::content_type);
    if (it != original_req.end())
    {
      ARQMA_LOG(trace, "Content-Type: {}", it->value().to_string());
      req->set(http::field::content_type, it->value().to_string());
    }
  }

  req->body() = std::move(original_req.body());
  req->target(target);
  req->set(http::field::host, "file.arqma.com");

  req->prepare_payload();

  for (auto& el : headers_json.items())
    req->insert(el.key(), el.value());

  auto cb = [this](sn_response_t res)
  {
    ARQMA_LOG(trace, "Successful file proxy request!");
    if (res.raw_response)
    {
      this->response_ = *res.raw_response;
      ARQMA_LOG(trace, "Response: {}", this->response_);
    }
    else
    {
      ARQMA_LOG(debug, "No response from file server");
    }

    this->write_response();
  };

  make_https_request(ioc_, "https://file.arqma.com", req, cb);
}

void connection_t::process_swarm_req(boost::string_view target) {

    const request_t& req = this->request_.get();

    if (!validate_snode_request() && (target != "/swarms/ping_test/v1")) {
        return;
    }

    response_.set(ARQMA_SNODE_SIGNATURE_HEADER, security_.get_cert_signature());

    if (target == "/swarms/push_batch/v1") {
      response_.result(http::status::ok);
      service_node_.process_push_batch(req.body());
    } else if (target == "/swarms/storage_test/v1") {
        response_.result(http::status::bad_request);
        ARQMA_LOG(debug, "Got storage test request");

        using nlohmann::json;

        const json body = json::parse(req.body(), nullptr, false);

        if (body == nlohmann::detail::value_t::discarded) {
            ARQMA_LOG(debug, "Bad snode test request: invalid json");
            body_stream_ << "invalid json\n";
            response_.result(http::status::bad_request);
            return;
        }

        uint64_t blk_height;
        std::string msg_hash;

        try {
            blk_height = body.at("height").get<uint64_t>();
            msg_hash = body.at("hash").get<std::string>();
        } catch (...) {
            this->body_stream_
                << "Bad snode test request: missing fields in json";
            response_.result(http::status::bad_request);
            ARQMA_LOG(debug, "Bad snode test request: missing fields in json");
            return;
        }

        const auto it = header_.find(ARQMA_SENDER_SNODE_PUBKEY_HEADER);
        if (it != header_.end()) {
            const std::string& tester_pk = it->second;
            this->process_storage_test_req(blk_height, tester_pk, msg_hash);
        } else {
            ARQMA_LOG(debug, "Ignoring test request, no pubkey present");
        }
    } else if (target == "/swarms/blockchain_test/v1") {
        ARQMA_LOG(debug, "Got blockchain test request");

        using nlohmann::json;

        const json body = json::parse(req.body(), nullptr, false);

        if (body.is_discarded()) {
            ARQMA_LOG(debug, "Bad snode test request: invalid json");
            response_.result(http::status::bad_request);
            return;
        }

        bc_test_params_t params;

        uint64_t height = 0;

        try {
            params.max_height = body.at("max_height").get<uint64_t>();
            params.seed = body.at("seed").get<uint64_t>();

            if (body.find("height") != body.end()) {
                height = body.at("height").get<uint64_t>();
            } else {
                ARQMA_LOG(debug, "No tester height, defaulting to {}", height);
            }
        } catch (...) {
            response_.result(http::status::bad_request);
            ARQMA_LOG(debug, "Bad snode test request: missing fields in json");
            return;
        }

        const auto it = header_.find(ARQMA_SENDER_SNODE_PUBKEY_HEADER);
        if (it != header_.end()) {
            const std::string& tester_pk = it->second;
            delay_response_ = true;
            this->process_blockchain_test_req(height, tester_pk, params);
        } else {
            ARQMA_LOG(debug, "Ignoring test request, no pubkey present");
        }

    } else if (target == "/swarms/ping_test/v1") {
        ARQMA_LOG(debug, "Received ping_test");
        response_.result(http::status::ok);
    } else if (target == "/swarms/push/v1") {

        ARQMA_LOG(trace, "swarms/push");

        /// NOTE:: we only expect one message here, but
        /// for now lets reuse the function we already have
        std::vector<message_t> messages = deserialize_messages(req.body());
        assert(messages.size() == 1);

        service_node_.process_push(messages.front());

        response_.result(http::status::ok);
    }
    else if (target == "/swarms/proxy_exit")
    {
      ARQMA_LOG(debug, "Processing proxy request: we are the destination node");

      const auto it = req.find(ARQMA_SENDER_KEY_HEADER);
      if (it != req.end())
      {
        const std::string key = {it->value().data(), it->value().size()};
        const auto plaintext = this->channel_cipher_.decrypt(req.body(), key);
        try
        {
          const json req = json::parse(plaintext, nullptr, true);
          const auto body = req.at("body").get<std::string>();
          this->response_modifier_ = [this, key](response_t& res)
          {
            nlohmann::json json_res;
            json_res["status"] = res.result_int();
            json_res["body"] = res.body();

            nlohmann::json headers;
            for (const auto& field : res)
            {
              std::string name = field.name_string().to_string();
              headers[std::move(name)] = field.value();
            }

            json_res["headers"] = headers;
            const std::string res_body = json_res.dump();
            res.body() = util::base64_encode(this->channel_cipher_.encrypt(res_body, key));
            res.result(http::status::ok);
          };

          ARQMA_LOG(trace, "CLIENT HEADERS: \n\t{}", req.at("headers").dump(2));

          const auto headers_it = req.find("headers");
          if (headers_it != req.end())
          {
            const auto long_poll_it = headers_it->find(ARQMA_LONG_POLL_HEADER);
            if (long_poll_it != headers_it->end())
            {
              const bool val = long_poll_it->get<bool>();
              ARQMA_LOG(info, "field: {}: {}", ARQMA_LONG_POLL_HEADER, val);
              this->header_.insert({ARQMA_LONG_POLL_HEADER, val ? "true" : "false"});
            }
          }

          this->process_client_req(body);

        } catch (std::exception& e)
        {
          ARQMA_LOG(error, "JSON parsing error: {}", e.what());
        }
      }
    }
}

// Determine what needs to be done with the request message.
void connection_t::process_request() {

    const request_t& req = this->request_.get();
    /// This method is responsible for filling out response_

    ARQMA_LOG(trace, "connection_t::process_request");
    response_.version(req.version());
    response_.keep_alive(false);

    /// TODO: make sure that we always send a response!

    response_.result(http::status::internal_server_error);

    const auto target = req.target();
    switch (req.method()) {
    case http::verb::post: {
        std::string reason;

        if (target == "/swarms/ping_test/v1") {
          this->process_swarm_req(target);
          break;
        }
        if (!service_node_.snode_ready(reason)) {
            ARQMA_LOG(debug,
                      "Ignoring post request; storage server not ready: {}",
                      reason);
            response_.result(http::status::service_unavailable);
            body_stream_ << fmt::format("Service node is not ready: {}\n",
                                        reason);
            break;
        }
        if (target == "/storage_rpc/v1") {
            /// Store/load from clients
            ARQMA_LOG(trace, "POST /storage_rpc/v1");

            try {
                process_client_req_rate_limited();
            } catch (std::exception& e) {
                this->body_stream_ << fmt::format(
                    "Exception caught while processing client request: {}",
                    e.what());
                response_.result(http::status::internal_server_error);
                ARQMA_LOG(
                    critical,
                    "Exception caught while processing client request: {}",
                    e.what());
            }

            // TODO: parse target (once) to determine if it is a "swarms" call
        } else if (target == "/swarms/push/v1") {
            this->process_swarm_req(target);
        } else if (target == "/swarms/push_batch/v1") {
            this->process_swarm_req(target);
        } else if (target == "/swarms/storage_test/v1") {

            this->process_swarm_req(target);

        } else if (target == "/swarms/blockchain_test/v1") {

            this->process_swarm_req(target);

        } else if (target == "/proxy") {
          this->process_proxy_req();
        } else if (target == "/file_proxy") {
          this->process_file_proxy_req();
        } else if (target == "/swarms/proxy_exit") {
          this->process_swarm_req(target);

        }
        else {
            ARQMA_LOG(debug, "unknown target for POST: {}", target.to_string());
            this->body_stream_ << fmt::format("unknown target for POST: {}",
                                              target.to_string());
            response_.result(http::status::not_found);
        }
        break;
    }
    case http::verb::get:

        if (target == "/get_stats/v1") {
            this->on_get_stats();
        } else if (target == "/get_logs/v1") {
            this->on_get_logs();
        } else {
            this->body_stream_ << fmt::format("unknown target for GET: {}",
                                              target.to_string());
            ARQMA_LOG(debug, "unknown target for GET: {}", target.to_string());
            response_.result(http::status::not_found);
        }
        break;
    default:
        ARQMA_LOG(debug, "bad request");
        response_.result(http::status::bad_request);
        break;
    }
}

static std::string obfuscate_pubkey(const std::string& pk) {
    std::string res = pk.substr(0, 2);
    res += "...";
    res += pk.substr(pk.length() - 3, pk.length() - 1);
    return res;
}

// Asynchronously transmit the response message.
void connection_t::write_response() {

#ifndef DISABLE_ENCRYPTION
    const auto it = header_.find(ARQMA_EPHEMKEY_HEADER);
    // TODO: do we need to separately handle the case where we can't find the
    // key?
    if (it != header_.end()) {
        const std::string& ephemKey = it->second;
        try {
            auto body = channel_cipher_.encrypt(body_stream_.str(), ephemKey);
            response_.body() = boost::beast::detail::base64_encode(body);
            response_.set(http::field::content_type, "text/plain");
        } catch (const std::exception& e) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "text/plain");
            body_stream_ << "Could not encrypt/encode response: ";
            body_stream_ << e.what() << "\n";
            ARQMA_LOG(
                critical,
                "Internal Server Error. Could not encrypt response for {}",
                obfuscate_pubkey(ephemKey));
        }
    }
#else
    const std::string body_stream = body_stream_.str();
    if (!body_stream.empty())
    {
      if (!response_.body().empty())
        ARQMA_LOG(debug, "Overwriting non-empty body in response!");

      response_.body() = body_stream_.str();
    }
#endif

    response_.set(http::field::content_length, std::to_string(response_.body().size()));

    /// This attempts to write all data to a stream
    /// TODO: handle the case when we are trying to send too much
    http::async_write(
        stream_, response_, [self = shared_from_this()](error_code ec, size_t) {
            if (ec && ec != boost::asio::error::operation_aborted) {
                ARQMA_LOG(error, "Failed to write to a socket: {}",
                          ec.message());
            }

            self->clean_up();
            /// Is it too early to cancel the deadline here?
            self->deadline_.cancel();
        });
}

bool connection_t::parse_header(const char* key) {
    const auto it = request_.get().find(key);
    if (it == request_.get().end()) {
        body_stream_ << "Missing field in header : " << key << "\n";
        return false;
    }
    header_[key] = it->value().to_string();
    return true;
}

template <typename... Args>
bool connection_t::parse_header(const char* first, Args... args) {
    return parse_header(first) && parse_header(args...);
}

json snodes_to_json(const std::vector<sn_record_t>& snodes) {

    json res_body;
    json snodes_json = json::array();

    for (const auto& sn : snodes) {
        json snode;
        snode["address"] = sn.sn_address();
        snode["pubkey_x25519"] = sn.pubkey_x25519_hex();
        snode["pubkey_ed25519"] = sn.pubkey_ed25519_hex();
        snode["port"] = std::to_string(sn.port());
        snode["ip"] = sn.ip();
        snodes_json.push_back(snode);
    }

    res_body["snodes"] = snodes_json;

    return res_body;
}

void connection_t::process_store(const json& params) {

    constexpr const char* fields[] = {"pubKey", "ttl", "timestamp", "data"};

    for (const auto& field : fields) {
        if (!params.contains(field)) {
            response_.result(http::status::bad_request);
            body_stream_ << fmt::format("invalid json: no `{}` field\n", field);
            ARQMA_LOG(debug, "Bad client request: no `{}` field", field);
            return;
        }
    }

    const auto ttl = params["ttl"].get<std::string>();
    const auto timestamp = params["timestamp"].get<std::string>();
    const auto data = params["data"].get<std::string>();

    bool created;
    auto pk =
        user_pubkey_t::create(params["pubKey"].get<std::string>(), created);

    if (!created) {
        response_.result(http::status::bad_request);
        body_stream_ << fmt::format("Pubkey must be {} characters long\n",
                                    get_user_pubkey_size());
        ARQMA_LOG(error, "Pubkey must be {} characters long", get_user_pubkey_size());
        return;
    }

    if (data.size() > MAX_MESSAGE_BODY) {
        response_.result(http::status::bad_request);
        body_stream_ << "Message body exceeds maximum allowed length of "
                     << MAX_MESSAGE_BODY << "\n";
        ARQMA_LOG(debug, "Message body too long: {}", data.size());
        return;
    }

    if (!service_node_.is_pubkey_for_us(pk)) {
        handle_wrong_swarm(pk);
        return;
    }

    uint64_t ttlInt;
    if (!util::parseTTL(ttl, ttlInt)) {
        response_.result(http::status::forbidden);
        response_.set(http::field::content_type, "text/plain");
        body_stream_ << "Provided TTL is not valid.\n";
        ARQMA_LOG(debug, "Forbidden. Invalid TTL: {}", ttl);
        return;
    }
    uint64_t timestampInt;
    if (!util::parseTimestamp(timestamp, ttlInt, timestampInt)) {
        response_.result(http::status::not_acceptable);
        response_.set(http::field::content_type, "text/plain");
        body_stream_ << "Timestamp error: check your clock\n";
        ARQMA_LOG(debug, "Forbidden. Invalid Timestamp: {}", timestamp);
        return;
    }
}

void connection_t::process_snodes_by_pk(const json& params) {

    if (!params.contains("pubKey")) {
        response_.result(http::status::bad_request);
        body_stream_ << "invalid json: no `pubKey` field\n";
        ARQMA_LOG(debug, "Bad client request: no `pubKey` field");
        return;
    }

    bool success;
    const auto pk =
        user_pubkey_t::create(params["pubKey"].get<std::string>(), success);
    if (!success) {
        response_.result(http::status::bad_request);
        body_stream_ << fmt::format("Pubkey must be {} characters long\n",
                                    get_user_pubkey_size());
        ARQMA_LOG(debug, "Pubkey must be {} characters long ",
                  get_user_pubkey_size());
        return;
    }

    const std::vector<sn_record_t> nodes = service_node_.get_snodes_by_pk(pk);
    const json res_body = snodes_to_json(nodes);

    response_.result(http::status::ok);
    response_.set(http::field::content_type, "application/json");

    /// This might throw if not utf-8 endoded
    body_stream_ << res_body.dump();
}

void connection_t::process_retrieve_all() {

    std::vector<Item> all_entries;

    bool res = service_node_.get_all_messages(all_entries);

    if (!res) {
        this->body_stream_ << "could not retrieve all entries\n";
        response_.result(http::status::internal_server_error);
        return;
    }

    json messages = json::array();

    for (auto& entry : all_entries) {
        json item;
        item["data"] = entry.data;
        item["pk"] = entry.pub_key;
        messages.push_back(item);
    }

    json res_body;
    res_body["messages"] = messages;

    body_stream_ << res_body.dump();
    response_.result(http::status::ok);
}

void connection_t::handle_wrong_swarm(const user_pubkey_t& pubKey) {

    const std::vector<sn_record_t> nodes =
        service_node_.get_snodes_by_pk(pubKey);
    const json res_body = snodes_to_json(nodes);

    response_.result(http::status::misdirected_request);
    response_.set(http::field::content_type, "application/json");

    /// This might throw if not utf-8 endoded
    body_stream_ << res_body.dump();
    ARQMA_LOG(debug, "Client request for different swarm received");
}

constexpr auto LONG_POLL_TIMEOUT = std::chrono::milliseconds(20000);

template <typename T>
void connection_t::respond_with_messages(const std::vector<T>& items) {

    json res_body;
    json messages = json::array();

    for (const auto& item : items) {
        json message;
        message["hash"] = item.hash;
        /// TODO: calculate expiration time once only?
        message["expiration"] = item.timestamp + item.ttl;
        message["data"] = item.data;
        messages.push_back(message);
    }

    res_body["messages"] = messages;

    response_.result(http::status::ok);
    response_.set(http::field::content_type, "application/json");
    body_stream_ << res_body.dump();

    this->write_response();
}

void connection_t::poll_db(const std::string& pk,
                           const std::string& last_hash) {

    std::vector<Item> items;

    if (!service_node_.retrieve(pk, last_hash, items)) {
        response_.result(http::status::internal_server_error);
        response_.set(http::field::content_type, "text/plain");
        ARQMA_LOG(critical,
                  "Internal Server Error. Could not retrieve messages for {}",
                  obfuscate_pubkey(pk));
        return;
    }

    const bool lp_requested = header_.find(ARQMA_LONG_POLL_HEADER) != header_.end();

    if (!items.empty()) {
        ARQMA_LOG(trace, "Successfully retrieved messages for {}",
                  obfuscate_pubkey(pk));
    }

    if (items.empty() && lp_requested) {

        auto self = shared_from_this();

        // Instead of responding immediately, we delay the response
        // until new data arrives for this PubKey
        service_node_.register_listener(pk, self);

        notification_ctx_ = notification_context_t{
            boost::asio::steady_timer{ioc_}, boost::none, pk};

        notification_ctx_->timer.expires_after(LONG_POLL_TIMEOUT);
        notification_ctx_->timer.async_wait([=](const error_code& ec) {
            if (ec == boost::asio::error::operation_aborted) {
                ARQMA_LOG(trace, "Notification timer manually triggered");
                // we use timer cancellation as notification mechanism
                std::vector<message_t> items;
                auto msg = notification_ctx_->message;
                if (msg) {
                    items.push_back(*msg);
                }

                respond_with_messages(items);
            } else {
                ARQMA_LOG(trace, "Notification timer expired");
                // If we are here, the notification timer expired
                // with no messages ready
                respond_with_messages<Item>({});
            }

            service_node_.remove_listener(pk, self.get());
        });

    } else {
        respond_with_messages(items);
    }
}

void connection_t::process_retrieve(const json& params) {

    service_node_.all_stats_.bump_retrieve_requests();

    constexpr const char* fields[] = {"pubKey", "lastHash"};

    for (const auto& field : fields) {
        if (!params.contains(field)) {
            response_.result(http::status::bad_request);
            body_stream_ << fmt::format("invalid json: no `{}` field\n", field);
            ARQMA_LOG(debug, "Bad client request: no `{}` field", field);
            return;
        }
    }

    bool success;
    const auto pk =
        user_pubkey_t::create(params["pubKey"].get<std::string>(), success);

    if (!success) {
        response_.result(http::status::bad_request);
        body_stream_ << fmt::format("PubKey must be {} characters long\n",
                                    get_user_pubkey_size());
        ARQMA_LOG(debug, "Pubkey must be {} characters long ",
                  get_user_pubkey_size());
        return;
    }

    if (!service_node_.is_pubkey_for_us(pk)) {
        handle_wrong_swarm(pk);
        return;
    }

    const auto last_hash = params["lastHash"].get<std::string>();

    // we are going to send the response anynchronously
    // once we have new data
    delay_response_ = true;

    poll_db(pk.str(), last_hash);
}

void connection_t::process_client_req(const std::string& req_json)
{
  const json body = json::parse(req_json, nullptr, false);
  if (body == nlohmann::detail::value_t::discarded)
  {
    response_.result(http::status::bad_request);
    body_stream_ << "invalid json\n";
    ARQMA_LOG(debug, "Bad client request: invalid json");
    return;
  }

  const auto method_it = body.find("method");
  if (method_it == body.end() || !method_it->is_string())
  {
    response_.result(http::status::bad_request);
    body_stream_ << "invalid json: no `method` field\n";
    ARQMA_LOG(debug, "Bad client request: no method field");
    return;
  }

  const auto method_name = method_it->get<std::string>();

  const auto params_it = body.find("params");
  if (params_it == body.end() || !params_it->is_object())
  {
    response_.result(http::status::bad_request);
    body_stream_ << "invalid json: no `params` field\n";
    ARQMA_LOG(debug, "Bad client request: no params field");
    return;
  }

  if (method_name == "store")
  {
    ARQMA_LOG(trace, "Process client request: store, connection: {}", this->conn_idx);
    this->process_store(*params_it);
  } else if (method_name == "retrieve")
  {
    ARQMA_LOG(trace, "Process client request: retrieve, connection: {}", this->conn_idx);
    this->process_retrieve(*params_it);
  } else if (method_name == "get_snodes_for_pubkey")
  {
    ARQMA_LOG(trace, "Process client request: snode for pubkey");
    this->process_snodes_by_pk(*params_it);
  }
  else
  {
    response_.result(http::status::bad_request);
    body_stream_ << "no method" << method_name << "\n";
    ARQMA_LOG(debug, "Bad client request: unknown method '{}'", method_name);
  }
}

void connection_t::process_client_req_rate_limited()
{
  const request_t& req = this->request_.get();
  std::string plain_text = req.body();
  const std::string client_ip = socket_.remote_endpoint().address().to_string();
  if (rate_limiter_.should_rate_limit_client(client_ip))
  {
    this->body_stream_ << "too many requests\n";
    response_.result(http::status::too_many_requests);
    ARQMA_LOG(debug, "Rate limiting client request.");
    return;
  }

#ifndef DISABLE_ENCRYPTION
  if (!parse_header(ARQMA_EPHEMKEY_HEADER))
  {
    ARQMA_LOG(debug, "Bad client request: could not parse headers");
    return;
  }

  try
  {
    const std::string decoded = boost::beast::detail::base64_decode(plain_text);
    plain_text = channel_cipher_.decrypt(decoded, header_[ARQMA_EPHEMKEY_HEADER]);
  }
  catch (const std::exception& e)
  {
    response_.result(http::status::bad_request);
    response_.set(http::field::content_type, "text/plain");
    body_stream_ << "Could not decode/decrypy body: ";
    body_stream_ << e.what() << "\n";
    ARQMA_LOG(debug, "Bad client request: could not decrypt body");
    return;
  }
#endif

  if (req.find(ARQMA_LONG_POLL_HEADER) != req.end())
  {
    header_[ARQMA_LONG_POLL_HEADER] = req.at(ARQMA_LONG_POLL_HEADER).to_string();
  }

  this->process_client_req(plain_text);
}

void connection_t::register_deadline() {

    auto self = shared_from_this();

    // Note: deadline callback captures a shared pointer to this, so
    // the connection will not be destroyed until the timer goes off.
    // If we want to destroy it earlier, we need to manually cancel the timer.
    deadline_.async_wait([self = std::move(self)](error_code ec) {
        const bool cancelled =
            (ec && ec == boost::asio::error::operation_aborted);

        if (cancelled)
            return;

        // Note: cancelled timer does absolutely nothing, so we need to make
        // sure we close the socket (and unsubscribe from notifications)
        // elsewhere if we cancel it.
        if (ec) {
            ARQMA_LOG(error, "Deadline timer error [{}]: {}", ec.value(),
                      ec.message());
        }

        ARQMA_LOG(debug, "Closing [connection_t] socket due to timeout");
        self->clean_up();
    });
}

void connection_t::do_close() {
    // Perform the SSL shutdown
    stream_.async_shutdown(std::bind(
        &connection_t::on_shutdown, shared_from_this(), sp::_1));
}

void connection_t::on_shutdown(boost::system::error_code ec) {
    if (ec == boost::asio::error::eof) {
        // Rationale:
        // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
        ec.assign(0, ec.category());
    } else if (ec) {
        ARQMA_LOG(debug, "Could not close ssl stream gracefully, ec: {} ({})", ec.message(), ec.value());
    }

    const auto sockfd = stream_.lowest_layer().native_handle();
    ARQMA_LOG(debug, "Close https socket: {}", sockfd);
    get_net_stats().record_socket_close(sockfd);
    stream_.lowest_layer().close();
}

void connection_t::on_get_stats() {
    this->body_stream_ << service_node_.get_stats();
    this->response_.result(http::status::ok);
}

void connection_t::on_get_logs() {

    /// Limit this call to 1 request per second
    static time_t last_req_time = 0;
    const time_t now = time(nullptr);
    constexpr time_t PERIOD = 1;

    if (now - last_req_time < PERIOD) {
        this->body_stream_ << "Too many request, try again later.";
        this->response_.result(http::status::too_many_requests);
        return;
    }

    last_req_time = now;

    auto dev_sink = dynamic_cast<arqma::dev_sink_mt*>(
        spdlog::get("arqma_logger")->sinks()[2].get());

    if (dev_sink == nullptr) {
        ARQMA_LOG(critical, "Sink #3 should be dev sink");
        assert(false);
        this->body_stream_ << "Developer error: sink #3 is not a dev sink.";
        this->response_.result(http::status::not_implemented);
        return;
    }

    nlohmann::json val;
    val["entries"] = dev_sink->peek();
    this->body_stream_ << val.dump(4);
    this->response_.result(http::status::ok);
}

/// ============

} // namespace http_server

/// TODO: make generic, avoid message copy
HttpClientSession::HttpClientSession(boost::asio::io_context& ioc,
                                     const tcp::endpoint& ep,
                                     const std::shared_ptr<request_t>& req,
                                     http_callback_t&& cb)
    : ioc_(ioc), socket_(ioc), endpoint_(ep), callback_(cb),
      deadline_timer_(ioc), req_(req) {
    get_net_stats().http_connections_out++;
}

void HttpClientSession::on_connect()
{
  const auto sockfd = socket_.native_handle();
  ARQMA_LOG(debug, "Open http socket: {}", sockfd);
  get_net_stats().record_socket_open(sockfd);
  http::async_write(socket_, *req_, std::bind(&HttpClientSession::on_write,
                    shared_from_this(), sp::_1, sp::_2));
}

void HttpClientSession::on_write(error_code ec, size_t bytes_transferred) {

    ARQMA_LOG(trace, "on write");
    if (ec) {
        ARQMA_LOG(error, "Http error on write, ec: {}. Message: {}", ec.value(),
                  ec.message());
        trigger_callback(SNodeError::ERROR_OTHER, nullptr);
        return;
    }

    ARQMA_LOG(trace, "Successfully transferred {} bytes", bytes_transferred);

    // Receive the HTTP response
    http::async_read(socket_, buffer_, res_, std::bind(&HttpClientSession::on_read,
                     shared_from_this(), sp::_1, sp::_2));
}

void HttpClientSession::on_read(error_code ec, size_t bytes_transferred) {

    if (!ec || (ec == http::error::end_of_stream)) {

        ARQMA_LOG(trace, "Successfully received {} bytes.", bytes_transferred);

        if (http::to_status_class(res_.result_int()) ==
            http::status_class::successful) {
            std::shared_ptr<std::string> body =
                std::make_shared<std::string>(res_.body());
            trigger_callback(SNodeError::NO_ERROR, std::move(body));
        } else {
            ARQMA_LOG(error, "Http request failed, error code: {}",
                      res_.result_int());
            trigger_callback(SNodeError::HTTP_ERROR, nullptr);
        }

    } else {

        if (ec != boost::asio::error::operation_aborted) {
            ARQMA_LOG(error, "Error on read: {}. Message: {}", ec.value(),
                      ec.message());
        }
        trigger_callback(SNodeError::ERROR_OTHER, nullptr);
    }
}

void HttpClientSession::start()
{
  socket_.async_connect(endpoint_, [this, self = shared_from_this()](const error_code& ec)
  {
    if (ec) {
      if (ec == boost::system::errc::connection_refused) {
        ARQMA_LOG(debug, "[http client]: count not connect to {}:{}, message: {} ({})",
                  endpoint_.address().to_string(), endpoint_.port(),
                  ec.message(), ec.value());
      } else {
        ARQMA_LOG(error, "[http client]: could not connect to {}:{}, message: {} ({})",
                  endpoint_.address().to_string(), endpoint_.port(),
                  ec.message(), ec.value());
      }

      trigger_callback(SNodeError::NO_REACH, nullptr);
      return;
    }

    self->on_connect();
  });

  deadline_timer_.expires_after(SESSION_TIME_LIMIT);
  deadline_timer_.async_wait([self = shared_from_this()](const error_code& ec)
  {
    if (ec) {
      if (ec != boost::asio::error::operation_aborted) {
        ARQMA_LOG(error, "Deadline timer failed in http client session [{}: {}]",
                  ec.value(), ec.message());
      }
    } else {
      ARQMA_LOG(debug, "client socket timed out");
      self->clean_up();
    }
  });
}

void HttpClientSession::trigger_callback(SNodeError error,
                                         std::shared_ptr<std::string>&& body) {
    ARQMA_LOG(trace, "Trigger callback");
    ioc_.post(std::bind(callback_, sn_response_t{error, body, boost::none}));
    used_callback_ = true;
    deadline_timer_.cancel();
}

void HttpClientSession::clean_up() {
  if (!needs_cleanup) {
    ARQMA_LOG(debug, "No need for cleanup");
    return;
  }

  needs_cleanup = false;

  if (!socket_.is_open()) {
    ARQMA_LOG(critical, "Socket is already closed");
    return;
  }

  error_code ec;

  socket_.shutdown(tcp::socket::shutdown_both, ec);
  if (ec && ec != boost::system::errc::not_connected) {
    ARQMA_LOG(error, "Socket shutdown failure [{}: {}]", ec.value(), ec.message());
  }

  const auto sockfd = socket_.native_handle();
  socket_.close(ec);

  if (ec) {
    ARQMA_LOG(error, "Closing socket {} failed [{}: {}]", sockfd, ec.value(), ec.message());
  } else {
    ARQMA_LOG(debug, "Close http socket: {}", sockfd);
    get_net_stats().record_socket_close(sockfd);
  }
}

/// We execute callback (if haven't already) here to make sure it is called
HttpClientSession::~HttpClientSession() {
  if (!used_callback_) {
    ioc_.post(std::bind(callback_, sn_response_t{SNodeError::ERROR_OTHER, nullptr}));
  }

  get_net_stats().http_connections_out--;

  this->clean_up();
}

} // namespace arqma
