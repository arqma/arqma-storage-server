#include "dns_text_records.h"
#include <nlohmann/json.hpp>
#include "version.h"
#include <netinet/in.h>
#include <resolv.h>

#include <boost/algorithm/string.hpp>

using json = nlohmann::json;

static constexpr char LATEST_VERSION_URL[] = "storage.version.arqma.com";

namespace arqma {

namespace dns {

static std::string get_dns_record(const char* url, std::error_code& ec) {

    std::string data;
    unsigned char query_buffer[1024] = {};

    // don't want to assume that ec has default value
    ec = std::error_code{};

    int response =
        res_query(url, ns_c_in, ns_t_txt, query_buffer, sizeof(query_buffer));

    if (response == -1) {
        ARQMA_LOG(warn, "res_query failed while retrieving dns entry");
        ec = std::make_error_code(std::errc::bad_message);
        return data;
    }

    ns_msg nsMsg;

    if (ns_initparse(query_buffer, response, &nsMsg) == -1) {
        ARQMA_LOG(warn, "ns_initparse failed while retrieving dns entry");
        ec = std::make_error_code(std::errc::bad_message);
        return data;
    }

    // We get back a sequence of N...[N...] values where N is a byte indicating
    // the length of the immediately following ... data.
    const auto count = ns_msg_count(nsMsg, ns_s_an);

    constexpr size_t DNS_MAX_CHUNK_LENGTH = 255;

    data.reserve(DNS_MAX_CHUNK_LENGTH * count);
    for (int i = 0; i < count; i++) {
        ns_rr rr;
        if (ns_parserr(&nsMsg, ns_s_an, i, &rr) == -1) {
            ARQMA_LOG(warn, "ns_parserr failed while parsing dns entry");
            ec = std::make_error_code(std::errc::bad_message);
            return data;
        }
        auto* rdata = ns_rr_rdata(rr);
        data.append(reinterpret_cast<const char*>(rdata + 1), rdata[0]);
    }

    return data;
}

static std::string query_latest_version() {
    ARQMA_LOG(debug, "Querying Latest Version...");

    std::error_code ec;
    const std::string version_str = get_dns_record(LATEST_VERSION_URL, ec);

    if (ec) {
        return "";
    }

    return version_str;
}

using version_t = std::array<uint16_t, 3>;

static bool parse_version(const std::string& str, version_t& version_out) {
    std::vector<std::string> strs;
    strs.reserve(3);
    boost::split(strs, str, boost::is_any_of("."));
    if (strs.size() != 3)
        return false;

    for (size_t i = 0; i < 3; i++)
    {
      try {
        size_t pos = 0;
        version_out[i] = std::stoi(strs[i], &pos);
        if (pos != strs[i].size())
          return false;
      } catch (const std::invalid_argument&) {
        return false;
      } catch (const std::out_of_range&) {
        return false;
      }
    }

    return true;
}

void check_latest_version() {

    const auto latest_version_str = query_latest_version();

    if (latest_version_str.empty()) {
        ARQMA_LOG(warn, "Failed to retrieve or parse the latest version number "
                        "from DNS record");
        return;
    }

    version_t latest_version;
    if (!parse_version(latest_version_str, latest_version)) {
        ARQMA_LOG(warn, "Could not parse the latest version: {}",
                  latest_version_str);
        return;
    }

    if (STORAGE_SERVER_VERSION < latest_version) {
        ARQMA_LOG(warn,
                  "You are using an outdated version of the storage server "
                  "({}), please update to {}!",
                  STORAGE_SERVER_VERSION_STRING, latest_version_str);
    } else {
        ARQMA_LOG(debug,
                  "You are using the latest version of the storage server ({})",
                  STORAGE_SERVER_VERSION_STRING);
    }
}

} // namespace dns
} // namespace arqma
