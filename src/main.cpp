#include "at_client.hpp"
#include "response_parser.hpp"
#include "serial_port.hpp"
#include "string_utils.hpp"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <readline/history.h>
#include <readline/readline.h>

namespace {

struct Options {
    std::string device{"/dev/ttyUSB1"};
    int baud_rate{115200};
    int timeout_ms{3000};
    bool raw{false};
    bool interactive{false};
    bool monitor{false};
    bool list_parsers{false};
    std::vector<std::string> positional_args;
};

struct NamedCommand {
    std::string title;
    std::string command;
};

struct CommandPlan {
    std::optional<std::string> single_command;
    std::vector<NamedCommand> command_batch;
};

std::atomic_bool g_stop{false};

void handle_signal(int) {
    g_stop = true;
}

void print_usage(std::ostream& out, const char* argv0) {
    out << "Usage: " << argv0 << " [options] [subcommand]\n\n"
        << "Subcommands:\n"
        << "  info                       Show a modem status summary\n"
        << "  get <name>                 Query a named value, for example: get signal\n"
        << "  set <name> <value...>      Set a named value, for example: set cmee 2\n"
        << "  raw <AT command...>        Send an AT command explicitly\n"
        << "  <AT command...>            Backward-compatible direct AT mode\n\n"
        << "Options:\n"
        << "  -d, --device PATH       Serial AT control port (default: /dev/ttyUSB1)\n"
        << "  -b, --baud RATE         Baud rate (default: 115200)\n"
        << "  -t, --timeout-ms MS     Command timeout in milliseconds (default: 3000)\n"
        << "  -r, --raw               Also print raw modem response lines\n"
        << "  -i, --interactive       Start interactive shell\n"
        << "  -m, --monitor           Print unsolicited modem lines until Ctrl-C\n"
        << "  -l, --list-parsers      List commands with human-readable parsers\n"
        << "  -h, --help              Show this help text\n\n"
        << "Examples:\n"
        << "  " << argv0 << " info\n"
        << "  " << argv0 << " get signal\n"
        << "  " << argv0 << " set cmee 2\n"
        << "  " << argv0 << " set apn 1 internet\n"
        << "  " << argv0 << " raw AT+COPS?\n"
        << "  " << argv0 << " AT+CSQ\n";
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(std::cout, argv[0]);
            std::exit(0);
        }
        if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --device");
            }
            options.device = argv[++i];
            continue;
        }
        if (arg == "-b" || arg == "--baud") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --baud");
            }
            options.baud_rate = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "-t" || arg == "--timeout-ms") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --timeout-ms");
            }
            options.timeout_ms = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "-r" || arg == "--raw") {
            options.raw = true;
            continue;
        }
        if (arg == "-i" || arg == "--interactive") {
            options.interactive = true;
            continue;
        }
        if (arg == "-m" || arg == "--monitor") {
            options.monitor = true;
            continue;
        }
        if (arg == "-l" || arg == "--list-parsers") {
            options.list_parsers = true;
            continue;
        }

        options.positional_args.push_back(arg);
    }

    if (!options.monitor && options.positional_args.empty() && !options.list_parsers) {
        options.interactive = true;
    }

    return options;
}

void print_response_summary(const AtResponse& response, const bool raw) {
    const ParsedResponse parsed = parse_response(response);

    if (!parsed.summary_lines.empty()) {
        std::cout << "Interpretation:\n";
        for (const auto& line : parsed.summary_lines) {
            std::cout << "  " << line << '\n';
        }
    }

    if (!response.information_lines.empty() && (!parsed.handled || raw)) {
        std::cout << "Response lines:\n";
        for (const auto& line : response.information_lines) {
            std::cout << "  " << line << '\n';
        }
    }

    if (raw && !response.echoed_lines.empty()) {
        std::cout << "Echo lines:\n";
        for (const auto& line : response.echoed_lines) {
            std::cout << "  " << line << '\n';
        }
    }

    std::cout << "Final result: " << response.final_result << '\n';
}

bool looks_like_at_command(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return false;
    }

    if (starts_with_ci(trimmed, "AT")) {
        return true;
    }

    const char leader = trimmed.front();
    return leader == '+' || leader == '$' || leader == '&' || leader == '*';
}

std::string normalize_direct_command(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        throw std::runtime_error("missing AT command");
    }

    if (starts_with_ci(trimmed, "AT")) {
        return trimmed;
    }

    const char leader = trimmed.front();
    if (leader == '+' || leader == '$' || leader == '&' || leader == '*') {
        return "AT" + trimmed;
    }

    return trimmed;
}

std::string normalize_keyword(std::string_view input) {
    std::string keyword = to_upper(trim(input));

    if (starts_with_ci(keyword, "AT")) {
        keyword.erase(0, 2);
    }
    if (!keyword.empty() &&
        (keyword.front() == '+' || keyword.front() == '$' || keyword.front() == '&' ||
         keyword.front() == '*')) {
        keyword.erase(0, 1);
    }

    for (char& ch : keyword) {
        if (ch == '_' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
            ch = '-';
        }
    }

    return keyword;
}

std::vector<std::string> split_words(std::string_view input) {
    std::istringstream stream{std::string(input)};
    std::vector<std::string> words;
    for (std::string word; stream >> word;) {
        words.push_back(word);
    }
    return words;
}

const std::vector<std::string>& interactive_local_commands() {
    static const std::vector<std::string> commands{
        ":help",
        ":raw",
        ":timeout",
        ":quit",
        ":exit",
    };
    return commands;
}

const std::vector<std::string>& interactive_top_level_commands() {
    static const std::vector<std::string> commands{
        "info",
        "status",
        "get",
        "show",
        "set",
        "raw",
    };
    return commands;
}

const std::unordered_map<std::string, std::string>& get_command_aliases() {
    static const std::unordered_map<std::string, std::string> aliases{
        {"ID", "ATI"},
        {"IDENTITY", "ATI"},
        {"MANUFACTURER", "AT+CGMI"},
        {"MODEL", "AT+CGMM"},
        {"FIRMWARE", "AT+CGMR"},
        {"IMEI", "AT+CGSN"},
        {"IMSI", "AT+CIMI"},
        {"SIM", "AT+CPIN?"},
        {"PIN", "AT+CPIN?"},
        {"SIGNAL", "AT+CSQ"},
        {"CSQ", "AT+CSQ"},
        {"OPERATOR", "AT+COPS?"},
        {"COPS", "AT+COPS?"},
        {"REG", "AT+CEREG?"},
        {"NETWORK", "AT+CEREG?"},
        {"EPS-REG", "AT+CEREG?"},
        {"CEREG", "AT+CEREG?"},
        {"PS-REG", "AT+CGREG?"},
        {"CGREG", "AT+CGREG?"},
        {"CS-REG", "AT+CREG?"},
        {"CREG", "AT+CREG?"},
        {"ATTACH", "AT+CGATT?"},
        {"CGATT", "AT+CGATT?"},
        {"PDP", "AT+CGDCONT?"},
        {"CONTEXTS", "AT+CGDCONT?"},
        {"APN", "AT+CGDCONT?"},
        {"CGDCONT", "AT+CGDCONT?"},
        {"ACTIVE-CONTEXTS", "AT+CGACT?"},
        {"CGACT", "AT+CGACT?"},
        {"CLOCK", "AT+CCLK?"},
        {"CCLK", "AT+CCLK?"},
        {"ERRORS", "AT+CMEE?"},
        {"CMEE", "AT+CMEE?"},
        {"FUNCTIONALITY", "AT+CFUN?"},
        {"FUN", "AT+CFUN?"},
        {"CFUN", "AT+CFUN?"},
        {"ACTIVITY", "AT+CPAS"},
        {"CPAS", "AT+CPAS"},
        {"NETWORK-MODE", "AT+CNMP?"},
        {"NETMODE", "AT+CNMP?"},
        {"CNMP", "AT+CNMP?"},
    };
    return aliases;
}

std::vector<std::string> get_command_names() {
    std::vector<std::string> names;
    names.reserve(get_command_aliases().size());
    for (const auto& [name, unused_command] : get_command_aliases()) {
        (void)unused_command;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        names.push_back(std::move(lower));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

const std::vector<std::string>& interactive_set_targets() {
    static const std::vector<std::string> targets{
        "apn",
        "cmee",
        "cnmp",
        "dialmode",
        "netmode",
        "network-mode",
        "operator",
        "modem-reset",
        "reset",
        "usbnetmode",
    };
    return targets;
}

const std::vector<std::string>& interactive_set_values(std::string_view target) {
    static const std::vector<std::string> empty;
    static const std::vector<std::string> operator_values{"auto"};
    static const std::vector<std::string> network_mode_values{"auto", "gsm", "lte"};
    static const std::vector<std::string> binary_values{"0", "1"};

    const std::string key = normalize_keyword(target);
    if (key == "OPERATOR") {
        return operator_values;
    }
    if (key == "NETWORK-MODE" || key == "NETMODE" || key == "CNMP") {
        return network_mode_values;
    }
    if (key == "USBNETMODE" || key == "DIALMODE") {
        return binary_values;
    }
    return empty;
}

std::vector<std::string> interactive_matches(std::string_view prefix,
                                             const std::vector<std::string>& candidates) {
    std::vector<std::string> matches;
    const std::string normalized_prefix = to_upper(prefix);
    for (const auto& candidate : candidates) {
        if (normalized_prefix.empty() ||
            starts_with_ci(candidate, normalized_prefix)) {
            matches.push_back(candidate);
        }
    }
    return matches;
}

char* duplicate_match(const std::string& value) {
    char* copy = static_cast<char*>(std::malloc(value.size() + 1));
    if (copy == nullptr) {
        return nullptr;
    }
    std::memcpy(copy, value.c_str(), value.size() + 1);
    return copy;
}

char* interactive_completion_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static std::size_t index = 0;

    if (state == 0) {
        matches.clear();
        index = 0;

        const std::string line = rl_line_buffer != nullptr ? rl_line_buffer : "";
        std::vector<std::string> words = split_words(line);
        const bool trailing_space =
            !line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0;

        const int word_index = trailing_space ? static_cast<int>(words.size())
                                              : static_cast<int>(words.size()) - 1;

        if (word_index <= 0) {
            if (!text[0] || text[0] == ':') {
                const auto local_matches =
                    interactive_matches(text, interactive_local_commands());
                matches.insert(matches.end(), local_matches.begin(), local_matches.end());
            }

            if (!text[0] || text[0] != ':') {
                const auto top_level_matches =
                    interactive_matches(text, interactive_top_level_commands());
                matches.insert(matches.end(), top_level_matches.begin(), top_level_matches.end());
            }
        } else {
            const std::string first = normalize_keyword(words.front());
            if (first == "GET" || first == "SHOW") {
                matches = interactive_matches(text, get_command_names());
            } else if (first == "SET") {
                if (word_index == 1) {
                    matches = interactive_matches(text, interactive_set_targets());
                } else if (words.size() >= 2) {
                    matches = interactive_matches(text, interactive_set_values(words[1]));
                }
            } else if (first == "RAW" && word_index == 1) {
                matches = interactive_matches(text, std::vector<std::string>{"AT"});
            }
        }
    }

    if (index >= matches.size()) {
        return nullptr;
    }

    return duplicate_match(matches[index++]);
}

char** interactive_completion(const char* text, int start, int end) {
    (void)start;
    (void)end;
    return rl_completion_matches(text, interactive_completion_generator);
}

std::optional<std::string> read_interactive_line(const char* prompt) {
    char* raw_line = readline(prompt);
    if (raw_line == nullptr) {
        return std::nullopt;
    }

    std::string line{raw_line};
    std::free(raw_line);

    if (!trim(line).empty()) {
        add_history(line.c_str());
    }

    return line;
}

std::optional<std::string> resolve_get_command(std::string_view name) {
    const auto& aliases = get_command_aliases();
    const auto it = aliases.find(normalize_keyword(name));
    if (it == aliases.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string normalize_set_target(std::string_view input) {
    std::string target = trim(input);
    if (target.empty()) {
        throw std::runtime_error("missing setting name");
    }

    if (starts_with_ci(target, "AT")) {
        target.erase(0, 2);
        target = trim(target);
    }

    if (target.empty()) {
        throw std::runtime_error("invalid setting name");
    }

    const char leader = target.front();
    if (leader == '+' || leader == '$' || leader == '&' || leader == '*') {
        return target;
    }

    return "+" + to_upper(target);
}

std::string quote_string(std::string_view value) {
    const std::string trimmed = trim(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed;
    }
    return "\"" + trimmed + "\"";
}

std::string build_set_command(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        throw std::runtime_error("usage: set <name> <value...>");
    }

    const std::string key = normalize_keyword(args[0]);
    if (key == "APN") {
        if (args.size() < 3 || args.size() > 4) {
            throw std::runtime_error("usage: set apn <cid> <apn> [pdp_type]");
        }

        const std::string pdp_type = args.size() == 4 ? args[3] : "IP";
        return "AT+CGDCONT=" + args[1] + "," + quote_string(pdp_type) + "," +
               quote_string(args[2]);
    }

    if (key == "USBNETMODE") {
        if (args.size() != 2) {
            throw std::runtime_error("usage: set usbnetmode <0|1>");
        }
        return "AT$MYCONFIG=\"usbnetmode\"," + args[1];
    }

    if (key == "DIALMODE") {
        if (args.size() != 2) {
            throw std::runtime_error("usage: set dialmode <0|1>");
        }
        return "AT+DIALMODE=" + args[1];
    }

    if (key == "OPERATOR") {
        if (args.size() != 2 || normalize_keyword(args[1]) != "AUTO") {
            throw std::runtime_error("usage: set operator auto");
        }
        return "AT+COPS=0";
    }

    if (key == "NETWORK-MODE" || key == "NETMODE" || key == "CNMP") {
        if (args.size() != 2) {
            throw std::runtime_error("usage: set network-mode <auto|gsm|lte>");
        }

        const std::string mode = normalize_keyword(args[1]);
        if (mode == "AUTO") {
            return "AT+CNMP=2";
        }
        if (mode == "GSM") {
            return "AT+CNMP=13";
        }
        if (mode == "LTE") {
            return "AT+CNMP=38";
        }

        throw std::runtime_error("usage: set network-mode <auto|gsm|lte>");
    }

    if (key == "RESET" || key == "MODEM-RESET") {
        if (args.size() != 1) {
            throw std::runtime_error("usage: set reset");
        }
        return "AT+CFUN=1,1";
    }

    const std::vector<std::string> values{args.begin() + 1, args.end()};
    return "AT" + normalize_set_target(args[0]) + "=" + join_strings(values, ",");
}

std::vector<NamedCommand> default_info_commands() {
    return {
        {"Manufacturer", "AT+CGMI"},
        {"Model", "AT+CGMM"},
        {"Firmware", "AT+CGMR"},
        {"IMEI", "AT+CGSN"},
        {"SIM state", "AT+CPIN?"},
        {"Signal", "AT+CSQ"},
        {"Operator", "AT+COPS?"},
        {"EPS registration", "AT+CEREG?"},
        {"Packet attach", "AT+CGATT?"},
        {"PDP contexts", "AT+CGDCONT?"},
        {"Functionality", "AT+CFUN?"},
    };
}

CommandPlan build_command_plan(const std::vector<std::string>& args) {
    CommandPlan plan;
    if (args.empty()) {
        return plan;
    }

    const std::string first = normalize_keyword(args.front());
    if (first == "INFO" || first == "STATUS") {
        if (args.size() != 1) {
            throw std::runtime_error("usage: info");
        }
        plan.command_batch = default_info_commands();
        return plan;
    }

    if (first == "GET" || first == "SHOW") {
        if (args.size() != 2) {
            throw std::runtime_error("usage: get <name>");
        }

        const auto command = resolve_get_command(args[1]);
        if (!command) {
            throw std::runtime_error("unknown get target: " + args[1]);
        }

        plan.single_command = *command;
        return plan;
    }

    if (first == "SET") {
        const std::vector<std::string> set_args{args.begin() + 1, args.end()};
        plan.single_command = build_set_command(set_args);
        return plan;
    }

    if (first == "RAW") {
        if (args.size() < 2) {
            throw std::runtime_error("usage: raw <AT command...>");
        }

        const std::vector<std::string> raw_args{args.begin() + 1, args.end()};
        plan.single_command = normalize_direct_command(join_strings(raw_args, " "));
        return plan;
    }

    const std::string joined = join_strings(args, " ");
    if (!looks_like_at_command(joined)) {
        throw std::runtime_error("unknown command: " + args.front());
    }

    plan.single_command = normalize_direct_command(joined);
    return plan;
}

bool execute_single_command(AtClient& client,
                            const Options& options,
                            const std::string& command,
                            const std::string& title = {}) {
    if (!title.empty()) {
        std::cout << title << '\n';
        std::cout << "Command: " << command << '\n';
    }

    const AtResponse response = client.send_command(command, options.timeout_ms);
    print_response_summary(response, options.raw);
    return response.success;
}

int execute_plan(AtClient& client, const Options& options, const CommandPlan& plan) {
    if (plan.single_command) {
        return execute_single_command(client, options, *plan.single_command) ? 0 : 1;
    }

    bool all_success = true;
    for (std::size_t i = 0; i < plan.command_batch.size(); ++i) {
        if (i > 0) {
            std::cout << '\n';
        }

        const auto& item = plan.command_batch[i];
        all_success = execute_single_command(client, options, item.command, item.title) && all_success;
    }

    return all_success ? 0 : 1;
}

void print_interactive_help() {
    std::cout << "Interactive commands:\n"
              << "  info                show a modem status summary\n"
              << "  get <name>          run a named query such as: get signal\n"
              << "  set <name> <args>   set a named value such as: set cmee 2\n"
              << "  AT...               send a raw AT command directly\n"
              << "  :help               show this help\n"
              << "  :raw on|off         toggle raw modem line printing\n"
              << "  :timeout <ms>       set command timeout in milliseconds\n"
              << "  :quit               exit the shell\n";
}

void run_interactive(AtClient& client, Options& options) {
    std::cout << "Interactive modem shell on " << options.device << " @" << options.baud_rate
              << ". Use :help for local commands.\n";

    rl_attempted_completion_function = interactive_completion;
    rl_completion_append_character = ' ';

    while (true) {
        const auto input = read_interactive_line("simcom> ");
        if (!input) {
            break;
        }

        std::string line = trim(*input);
        if (line.empty()) {
            continue;
        }

        if (line == ":quit" || line == ":exit") {
            break;
        }
        if (line == ":help") {
            print_interactive_help();
            continue;
        }
        if (starts_with_ci(line, ":raw ")) {
            const std::string value = to_upper(trim(line.substr(5)));
            if (value == "ON") {
                options.raw = true;
                std::cout << "Raw output enabled.\n";
            } else if (value == "OFF") {
                options.raw = false;
                std::cout << "Raw output disabled.\n";
            } else {
                std::cout << "Use :raw on or :raw off.\n";
            }
            continue;
        }
        if (starts_with_ci(line, ":timeout ")) {
            try {
                options.timeout_ms = std::stoi(trim(line.substr(9)));
                std::cout << "Timeout set to " << options.timeout_ms << " ms.\n";
            } catch (const std::exception&) {
                std::cout << "Invalid timeout value.\n";
            }
            continue;
        }
        if (!line.empty() && line.front() == ':') {
            std::cout << "Unknown local command. Use :help.\n";
            continue;
        }

        try {
            std::vector<std::string> args;
            if (looks_like_at_command(line)) {
                args.push_back(line);
            } else {
                args = split_words(line);
            }

            const CommandPlan plan = build_command_plan(args);
            (void)execute_plan(client, options, plan);
        } catch (const std::exception& error) {
            std::cout << "Error: " << error.what() << '\n';
        }
    }
}

std::string timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
    localtime_r(&now, &local_time);

    std::ostringstream out;
    out << std::put_time(&local_time, "%F %T");
    return out.str();
}

void run_monitor(AtClient& client) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "Monitoring modem output. Press Ctrl-C to stop.\n";
    while (!g_stop) {
        for (const auto& line : client.read_lines(500)) {
            std::cout << timestamp_now() << "  " << line << '\n';
        }
    }
}

void print_supported_parsers() {
    std::cout << "Human-readable parsers are available for:\n";
    for (const auto& command : supported_parser_commands()) {
        std::cout << "  " << command << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_args(argc, argv);

        if (options.list_parsers) {
            print_supported_parsers();
            return 0;
        }

        std::optional<CommandPlan> plan;
        if (!options.monitor && !options.interactive && !options.positional_args.empty()) {
            plan = build_command_plan(options.positional_args);
        }

        SerialPort port;
        port.open(options.device, options.baud_rate);

        AtClient client(port);
        if (options.monitor) {
            run_monitor(client);
            return 0;
        }

        if (options.interactive) {
            run_interactive(client, options);
            return 0;
        }

        if (!plan) {
            throw std::runtime_error("nothing to do");
        }

        return execute_plan(client, options, *plan);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
