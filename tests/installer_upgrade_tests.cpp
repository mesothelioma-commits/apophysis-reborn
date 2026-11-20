// Host-side check of the same version selection and receipt reader used by Windows setup.
#include "upgrade_receipt.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace apo::installer;

void check(bool result, const char *message) {
    if (!result) throw std::runtime_error(message);
}

void versions() {
    const Version current{0, 12, 0}, previous{0, 11, 0}, older{0, 9, 9};
    check(parse_version(L"0.11.0") == previous, "three-part version not recognized");
    for (auto text : {L"", L"0.11", L"0.11.0-beta", L"ApophysisReborn", L"-1.1.0", L"0.65536.0", L"0.011.0"})
        check(!parse_version(text), "non-release version accepted");
    UpgradeSelection up(current);
    check(up.previous().empty(), "first install selected a predecessor");
    up.consider(older);
    up.consider(previous);
    up.consider(Version{0, 10, 21});
    check(up.previous() == std::vector<Version>{previous, {0, 10, 21}, older}, "did not select all previous releases in descending version order");
    up.consider(current);
    check(up.previous().empty(), "same-version run selected a predecessor");
    for (bool reverse : {false, true}) {
        UpgradeSelection down(current);
        down.consider(reverse ? Version{1, 0, 0} : previous);
        down.consider(reverse ? previous : Version{1, 0, 0});
        check(down.previous().empty(), "downgrade selected an older installation");
    }
}

bool rejects(const std::string &receipt, std::string_view version = "0.10.0") {
    try { (void)receipt_files(receipt, version); }
    catch (const std::runtime_error &) { return true; }
    return false;
}

void checksums() {
    const std::string nested(64, '1'), sibling(64, '2'), uppercase(64, '3'), manifest(64, '4');
    const std::map<std::string, std::string> entries{
        {"a/nested.txt", nested}, {"a-extra.txt", sibling}, {"Z.txt", uppercase}
    };
    // pathlib sorts whole components. The directory "a" precedes the file "a-extra.txt".
    const auto posix = uppercase + "  Z.txt\n" + nested + "  a/nested.txt\n" +
                       sibling + "  a-extra.txt\n" + manifest + "  package-manifest.json\n";
    const auto windows = nested + "  a/nested.txt\r\n" + sibling + "  a-extra.txt\r\n" +
                         manifest + "  package-manifest.json\r\n" + uppercase + "  Z.txt\r\n";
    check(receipt_checksums(entries, manifest, false) == posix, "POSIX checksum file bytes differ");
    check(receipt_checksums(entries, manifest, true) == windows, "Windows checksum file bytes differ");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected manifest path and example hash");
        versions();
        checksums();
        std::ifstream file(argv[1], std::ios::binary);
        check(file.is_open(), "could not open the packaging fixture");
        const std::string manifest((std::istreambuf_iterator<char>(file)), {});
        const auto entries = apo::installer::receipt_files(manifest, "0.10.0");
        const std::string example = "examples/a study \xc3\xa9 \xf0\x9f\x8c\x8c.flame";
        check(entries.size() == 4 && entries.at(example) == argv[2], "old release file identities changed");
        check(rejects(manifest, "0.10.1"), "accepted receipt from a different version");
        check(rejects(manifest + "{}"), "accepted trailing receipt data");
        auto changed = manifest;
        changed.replace(changed.find(argv[2]), 64, "missing checksum");
        check(rejects(changed), "accepted receipt without a usable checksum");
        const std::string path = "examples/normal file.flame";
        for (auto unsafe : {"../outside", "/outside", "folder//file", "C:/outside"}) {
            changed = manifest;
            changed.replace(changed.find(path), path.size(), unsafe);
            check(rejects(changed), "accepted non-relative receipt path");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
