#include "response_parser.hpp"

#include "string_utils.hpp"

#include <cstdlib>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace {

using Parser = ParsedResponse (*)(const AtResponse&);

std::optional<int> parse_int(std::string_view value) {
    const std::string text = trim(value);
    if (text.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::optional<std::string> find_prefixed_payload(const AtResponse& response,
                                                 const std::string& prefix) {
    for (const auto& line : response.information_lines) {
        if (starts_with_ci(line, prefix)) {
            const std::size_t pos = line.find(':');
            if (pos == std::string::npos) {
                return std::string{};
            }
            return trim(line.substr(pos + 1));
        }
    }
    return std::nullopt;
}

std::vector<std::string> find_prefixed_payloads(const AtResponse& response,
                                                const std::string& prefix) {
    std::vector<std::string> payloads;
    for (const auto& line : response.information_lines) {
        if (starts_with_ci(line, prefix)) {
            const std::size_t pos = line.find(':');
            payloads.push_back(pos == std::string::npos ? std::string{} : trim(line.substr(pos + 1)));
        }
    }
    return payloads;
}

std::string describe_cmee_error(const int code) {
    static const std::unordered_map<int, std::string> errors{
        {0, "phone failure"},          {1, "no connection to phone"},
        {3, "operation not allowed"},  {4, "operation not supported"},
        {10, "SIM not inserted"},      {11, "SIM PIN required"},
        {12, "SIM PUK required"},      {13, "SIM failure"},
        {14, "SIM busy"},              {15, "SIM wrong"},
        {16, "incorrect password"},    {17, "SIM PIN2 required"},
        {18, "SIM PUK2 required"},     {20, "memory full"},
        {21, "invalid index"},         {22, "not found"},
        {23, "memory failure"},        {24, "text string too long"},
        {25, "invalid characters in text string"},
        {26, "dial string too long"},  {27, "invalid characters in dial string"},
        {30, "no network service"},    {31, "network timeout"},
        {32, "network not allowed; emergency calls only"},
        {50, "incorrect parameters"},  {100, "unknown error"},
        {103, "illegal message"},      {106, "illegal ME"},
        {107, "GPRS services not allowed"},
        {111, "PLMN not allowed"},     {112, "location area not allowed"},
        {113, "roaming not allowed in this location area"},
        {132, "service option not supported"},
        {133, "requested service option not subscribed"},
        {134, "service option temporarily out of order"},
        {148, "unspecified GPRS error"},
        {149, "PDP authentication failure"},
        {150, "invalid mobile class"}, {151, "AT command timeout"},
        {535, "protocol stack busy"},
    };

    const auto it = errors.find(code);
    return it == errors.end() ? "unknown CME error" : it->second;
}

std::string describe_cms_error(const int code) {
    static const std::unordered_map<int, std::string> errors{
        {300, "ME failure"},
        {301, "SMS service of ME reserved"},
        {302, "operation not allowed"},
        {303, "operation not supported"},
        {304, "invalid PDU mode parameter"},
        {305, "invalid text mode parameter"},
        {310, "SIM not inserted"},
        {311, "SIM PIN required"},
        {312, "PH-SIM PIN required"},
        {313, "SIM failure"},
        {314, "SIM busy"},
        {315, "SIM wrong"},
        {316, "SIM PUK required"},
        {317, "SIM PIN2 required"},
        {318, "SIM PUK2 required"},
        {320, "memory failure"},
        {321, "invalid memory index"},
        {322, "memory full"},
        {330, "SMSC address unknown"},
        {331, "no network service"},
        {332, "network timeout"},
        {340, "no +CNMA acknowledgement expected"},
        {341, "buffer overflow"},
        {342, "SMS size more than expected"},
        {500, "unknown error"},
    };

    const auto it = errors.find(code);
    return it == errors.end() ? "unknown CMS error" : it->second;
}

std::string describe_rssi(const int rssi) {
    if (rssi == 99) {
        return "unknown";
    }
    if (rssi >= 0 && rssi <= 31) {
        const int dbm = -113 + (2 * rssi);
        return std::to_string(rssi) + " (~" + std::to_string(dbm) + " dBm)";
    }
    return std::to_string(rssi) + " (out of range)";
}

std::string describe_ber(const int ber) {
    switch (ber) {
    case 0:
        return "0 (<0.2%)";
    case 1:
        return "1 (0.2% to 0.4%)";
    case 2:
        return "2 (0.4% to 0.8%)";
    case 3:
        return "3 (0.8% to 1.6%)";
    case 4:
        return "4 (1.6% to 3.2%)";
    case 5:
        return "5 (3.2% to 6.4%)";
    case 6:
        return "6 (6.4% to 12.8%)";
    case 7:
        return "7 (>12.8%)";
    case 99:
        return "99 (unknown)";
    default:
        return std::to_string(ber) + " (reserved)";
    }
}

std::string describe_registration_state(const int stat) {
    switch (stat) {
    case 0:
        return "not registered, not searching";
    case 1:
        return "registered, home network";
    case 2:
        return "not registered, currently searching";
    case 3:
        return "registration denied";
    case 4:
        return "registration state unknown";
    case 5:
        return "registered, roaming";
    case 6:
        return "registered for SMS only, home network";
    case 7:
        return "registered for SMS only, roaming";
    case 8:
        return "emergency service only";
    case 9:
        return "registered for CSFB not preferred, home network";
    case 10:
        return "registered for CSFB not preferred, roaming";
    default:
        return std::to_string(stat) + " (vendor or reserved value)";
    }
}

std::string describe_access_technology(const int act) {
    switch (act) {
    case 0:
        return "GSM";
    case 2:
        return "UTRAN";
    case 4:
        return "GSM with EDGE";
    case 7:
        return "LTE (E-UTRAN)";
    case 9:
        return "LTE Cat-M / NB-IoT variant";
    case 10:
        return "E-UTRA connected to 5GCN";
    case 11:
        return "NR connected to 5GCN";
    case 12:
        return "NG-RAN";
    case 13:
        return "E-UTRA-NR dual connectivity";
    case 14:
        return "NR non-standalone";
    default:
        return std::to_string(act) + " (unknown or vendor specific)";
    }
}

std::string describe_operator_mode(const int mode) {
    switch (mode) {
    case 0:
        return "automatic";
    case 1:
        return "manual";
    case 2:
        return "deregister from network";
    case 3:
        return "set format only";
    case 4:
        return "manual, fall back to automatic";
    default:
        return std::to_string(mode) + " (unknown)";
    }
}

std::string describe_operator_format(const int format) {
    switch (format) {
    case 0:
        return "long alphanumeric";
    case 1:
        return "short alphanumeric";
    case 2:
        return "numeric";
    default:
        return std::to_string(format) + " (unknown)";
    }
}

std::string describe_cfun(const int fun) {
    switch (fun) {
    case 0:
        return "minimum functionality";
    case 1:
        return "full functionality";
    case 4:
        return "flight mode";
    default:
        return std::to_string(fun) + " (vendor specific)";
    }
}

std::string describe_cpas(const int state) {
    switch (state) {
    case 0:
        return "ready";
    case 2:
        return "unknown";
    case 3:
        return "ringing";
    case 4:
        return "call in progress";
    default:
        return std::to_string(state) + " (vendor specific)";
    }
}

ParsedResponse parse_error_result(const AtResponse& response) {
    ParsedResponse parsed;
    parsed.handled = true;

    if (response.timed_out) {
        parsed.summary_lines.push_back("Request timed out before the modem returned a final result.");
        return parsed;
    }

    parsed.summary_lines.push_back("Command failed: " + response.final_result);

    const std::string upper = to_upper(response.final_result);
    const std::size_t pos = response.final_result.find(':');
    if (pos == std::string::npos) {
        return parsed;
    }

    const std::string payload = trim(response.final_result.substr(pos + 1));
    if (starts_with_ci(upper, "+CME ERROR:")) {
        if (const auto code = parse_int(payload)) {
            parsed.summary_lines.push_back("CME error " + std::to_string(*code) + ": " +
                                           describe_cmee_error(*code));
        } else {
            parsed.summary_lines.push_back("CME error text: " + payload);
        }
        return parsed;
    }

    if (starts_with_ci(upper, "+CMS ERROR:")) {
        if (const auto code = parse_int(payload)) {
            parsed.summary_lines.push_back("CMS error " + std::to_string(*code) + ": " +
                                           describe_cms_error(*code));
        } else {
            parsed.summary_lines.push_back("CMS error text: " + payload);
        }
    }

    return parsed;
}

ParsedResponse parse_identity_lines(const AtResponse& response, const std::string& label) {
    ParsedResponse parsed;
    if (response.information_lines.empty()) {
        return parsed;
    }

    parsed.handled = true;
    if (response.information_lines.size() == 1) {
        parsed.summary_lines.push_back(label + ": " + response.information_lines.front());
        return parsed;
    }

    parsed.summary_lines.push_back(label + ": " + join_strings(response.information_lines, " | "));
    return parsed;
}

ParsedResponse parse_single_prefixed_value(const AtResponse& response,
                                           const std::string& prefix,
                                           const std::string& label) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, prefix);
    if (!payload) {
        return parsed;
    }

    parsed.handled = true;
    parsed.summary_lines.push_back(label + ": " + unquote(*payload));
    return parsed;
}

ParsedResponse parse_csq(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CSQ");
    if (!payload) {
        return parsed;
    }

    const auto fields = split_csv(*payload);
    if (fields.size() < 2) {
        return parsed;
    }

    parsed.handled = true;

    if (const auto rssi = parse_int(fields[0])) {
        parsed.summary_lines.push_back("Signal RSSI: " + describe_rssi(*rssi));
    }
    if (const auto ber = parse_int(fields[1])) {
        parsed.summary_lines.push_back("Signal BER: " + describe_ber(*ber));
    }

    return parsed;
}

ParsedResponse parse_cpin(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CPIN");
    if (!payload) {
        return parsed;
    }

    parsed.handled = true;
    parsed.summary_lines.push_back("SIM state: " + unquote(*payload));
    return parsed;
}

ParsedResponse parse_registration(const AtResponse& response,
                                  const std::string& prefix,
                                  const std::string& label) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, prefix);
    if (!payload) {
        return parsed;
    }

    const auto fields = split_csv(*payload);
    if (fields.empty()) {
        return parsed;
    }

    parsed.handled = true;

    std::size_t stat_index = 0;
    if (fields.size() > 1) {
        stat_index = 1;
        if (const auto n = parse_int(fields[0])) {
            parsed.summary_lines.push_back(label + " report mode: " + std::to_string(*n));
        }
    }

    if (const auto stat = parse_int(fields[stat_index])) {
        parsed.summary_lines.push_back(label + " state: " + describe_registration_state(*stat));
    }

    if (fields.size() > stat_index + 1 && !fields[stat_index + 1].empty()) {
        parsed.summary_lines.push_back(label + " LAC/TAC: " + unquote(fields[stat_index + 1]));
    }
    if (fields.size() > stat_index + 2 && !fields[stat_index + 2].empty()) {
        parsed.summary_lines.push_back(label + " cell ID: " + unquote(fields[stat_index + 2]));
    }
    if (fields.size() > stat_index + 3) {
        if (const auto act = parse_int(fields[stat_index + 3])) {
            parsed.summary_lines.push_back(label + " access technology: " +
                                           describe_access_technology(*act));
        }
    }

    return parsed;
}

ParsedResponse parse_creg(const AtResponse& response) {
    return parse_registration(response, "+CREG", "Circuit-switched registration");
}

ParsedResponse parse_cgreg(const AtResponse& response) {
    return parse_registration(response, "+CGREG", "Packet-switched registration");
}

ParsedResponse parse_cereg(const AtResponse& response) {
    return parse_registration(response, "+CEREG", "EPS registration");
}

ParsedResponse parse_cops(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+COPS");
    if (!payload) {
        return parsed;
    }

    const auto fields = split_csv(*payload);
    if (fields.empty()) {
        return parsed;
    }

    parsed.handled = true;

    if (const auto mode = parse_int(fields[0])) {
        parsed.summary_lines.push_back("Operator selection mode: " + describe_operator_mode(*mode));
    }
    if (fields.size() > 1) {
        if (const auto format = parse_int(fields[1])) {
            parsed.summary_lines.push_back("Operator format: " + describe_operator_format(*format));
        }
    }
    if (fields.size() > 2 && !fields[2].empty()) {
        parsed.summary_lines.push_back("Operator: " + unquote(fields[2]));
    }
    if (fields.size() > 3) {
        if (const auto act = parse_int(fields[3])) {
            parsed.summary_lines.push_back("Operator access technology: " +
                                           describe_access_technology(*act));
        }
    }

    return parsed;
}

ParsedResponse parse_cgatt(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CGATT");
    if (!payload) {
        return parsed;
    }

    parsed.handled = true;
    const auto state = parse_int(*payload);
    if (state && *state == 1) {
        parsed.summary_lines.push_back("Packet service attach: attached");
    } else if (state && *state == 0) {
        parsed.summary_lines.push_back("Packet service attach: detached");
    } else if (state) {
        parsed.summary_lines.push_back("Packet service attach: " + std::to_string(*state));
    }
    return parsed;
}

ParsedResponse parse_cgact(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payloads = find_prefixed_payloads(response, "+CGACT");
    if (payloads.empty()) {
        return parsed;
    }

    parsed.handled = true;
    for (const auto& payload : payloads) {
        const auto fields = split_csv(payload);
        if (fields.size() < 2) {
            continue;
        }

        const auto cid = parse_int(fields[0]);
        const auto state = parse_int(fields[1]);
        if (cid && state) {
            parsed.summary_lines.push_back("PDP context " + std::to_string(*cid) + ": " +
                                           (*state == 1 ? "active" : "inactive"));
        }
    }

    return parsed;
}

ParsedResponse parse_cgdcont(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payloads = find_prefixed_payloads(response, "+CGDCONT");
    if (payloads.empty()) {
        return parsed;
    }

    parsed.handled = true;
    for (const auto& payload : payloads) {
        const auto fields = split_csv(payload);
        if (fields.size() < 4) {
            continue;
        }

        std::ostringstream line;
        line << "PDP context " << unquote(fields[0]) << ": type=" << unquote(fields[1])
             << ", APN=" << unquote(fields[2]) << ", address=" << unquote(fields[3]);
        parsed.summary_lines.push_back(line.str());
    }

    return parsed;
}

ParsedResponse parse_cclk(const AtResponse& response) {
    return parse_single_prefixed_value(response, "+CCLK", "Modem clock");
}

ParsedResponse parse_cmee(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CMEE");
    if (!payload) {
        return parsed;
    }

    const auto mode = parse_int(*payload);
    if (!mode) {
        return parsed;
    }

    parsed.handled = true;
    switch (*mode) {
    case 0:
        parsed.summary_lines.push_back("CME error mode: disabled");
        break;
    case 1:
        parsed.summary_lines.push_back("CME error mode: numeric errors");
        break;
    case 2:
        parsed.summary_lines.push_back("CME error mode: verbose text errors");
        break;
    default:
        parsed.summary_lines.push_back("CME error mode: " + std::to_string(*mode));
        break;
    }
    return parsed;
}

ParsedResponse parse_cfun(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CFUN");
    if (!payload) {
        return parsed;
    }

    const auto fields = split_csv(*payload);
    if (fields.empty()) {
        return parsed;
    }

    const auto fun = parse_int(fields[0]);
    if (!fun) {
        return parsed;
    }

    parsed.handled = true;
    parsed.summary_lines.push_back("Functionality level: " + describe_cfun(*fun));
    return parsed;
}

ParsedResponse parse_cpas(const AtResponse& response) {
    ParsedResponse parsed;
    const auto payload = find_prefixed_payload(response, "+CPAS");
    if (!payload) {
        return parsed;
    }

    const auto state = parse_int(*payload);
    if (!state) {
        return parsed;
    }

    parsed.handled = true;
    parsed.summary_lines.push_back("Activity state: " + describe_cpas(*state));
    return parsed;
}

ParsedResponse parse_ati(const AtResponse& response) {
    return parse_identity_lines(response, "Identification");
}

const std::unordered_map<std::string, Parser>& parser_table() {
    static const std::unordered_map<std::string, Parser> parsers{
        {"I", &parse_ati},
        {"+CGMI", [](const AtResponse& response) { return parse_identity_lines(response, "Manufacturer"); }},
        {"+CGMM", [](const AtResponse& response) { return parse_identity_lines(response, "Model"); }},
        {"+CGMR", [](const AtResponse& response) { return parse_identity_lines(response, "Firmware"); }},
        {"+CGSN", [](const AtResponse& response) { return parse_identity_lines(response, "IMEI"); }},
        {"+CIMI", [](const AtResponse& response) { return parse_identity_lines(response, "IMSI"); }},
        {"+CPIN", &parse_cpin},
        {"+CSQ", &parse_csq},
        {"+CREG", &parse_creg},
        {"+CGREG", &parse_cgreg},
        {"+CEREG", &parse_cereg},
        {"+COPS", &parse_cops},
        {"+CGATT", &parse_cgatt},
        {"+CGACT", &parse_cgact},
        {"+CGDCONT", &parse_cgdcont},
        {"+CCLK", &parse_cclk},
        {"+CMEE", &parse_cmee},
        {"+CFUN", &parse_cfun},
        {"+CPAS", &parse_cpas},
    };
    return parsers;
}

} // namespace

ParsedResponse parse_response(const AtResponse& response) {
    if (!response.success || response.timed_out) {
        return parse_error_result(response);
    }

    const auto& parsers = parser_table();
    const std::string key = normalize_command_name(response.command);
    const auto it = parsers.find(key);
    if (it == parsers.end()) {
        return {};
    }

    return it->second(response);
}

std::vector<std::string> supported_parser_commands() {
    std::vector<std::string> commands{
        "ATI",        "AT+CGMI",   "AT+CGMM",   "AT+CGMR",  "AT+CGSN",
        "AT+CIMI",    "AT+CPIN?",  "AT+CSQ",    "AT+CREG?", "AT+CGREG?",
        "AT+CEREG?",  "AT+COPS?",  "AT+CGATT?", "AT+CGACT?","AT+CGDCONT?",
        "AT+CCLK?",   "AT+CMEE?",  "AT+CFUN?",  "AT+CPAS",
    };
    return commands;
}
