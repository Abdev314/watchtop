#include "InotifyParser.h"
#include <fstream>
#include <string>
#include <string_view>

std::vector<WatchDescriptor>
InotifyParser::parse_fdinfo(const std::string& path) {
    std::vector<WatchDescriptor> watches;

    std::ifstream file(path);
    if (!file.is_open()) return watches;

    std::string line;

    while (std::getline(file, line)) {
        constexpr std::string_view prefix = "inotify wd:";

        if (!line.starts_with(prefix))
            continue;

        const char* p = line.c_str() + prefix.size();
        char* end = nullptr;

        WatchDescriptor wd{};

        wd.wd = static_cast<int>(std::strtoul(p, &end, 10));
        if (end == p)
            continue;
        p = end;

        auto consume = [&](std::string_view field) -> bool {
            if (!std::string_view(p).starts_with(field))
                return false;

            p += field.size();
            return true;
        };

        if (consume(" ino:")) {
            wd.inode = std::strtoull(p, &end, 0);
            p = end;
        }

        if (consume(" sdev:")) {
            wd.sdev = std::strtoull(p, &end, 0);
            p = end;
        }

        if (consume(" mask:")) {
            wd.mask = static_cast<uint32_t>(std::strtoul(p, &end, 0));
            p = end;
        }

        watches.push_back(wd);
    }

    return watches;
}