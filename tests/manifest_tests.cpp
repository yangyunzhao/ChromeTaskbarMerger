#include <Windows.h>

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

[[nodiscard]] std::string ReadEmbeddedManifest(const HMODULE module) {
    const HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(1), RT_MANIFEST);
    if (resource == nullptr) {
        return {};
    }
    const DWORD size = SizeofResource(module, resource);
    const HGLOBAL loaded = LoadResource(module, resource);
    const void* const bytes = loaded == nullptr ? nullptr : LockResource(loaded);
    if (bytes == nullptr || size == 0) {
        return {};
    }
    return std::string(
        static_cast<const char*>(bytes),
        static_cast<std::size_t>(size));
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 2) {
        std::cerr << "Expected the ChromeTaskbarMerger executable path.\n";
        return 2;
    }

    DWORD binary_type = 0;
    Expect(GetBinaryTypeW(arguments[1], &binary_type) != FALSE &&
               binary_type == SCS_64BIT_BINARY,
           "the Phase 3 executable should be a native 64-bit PE image");

    const HMODULE module = LoadLibraryExW(
        arguments[1],
        nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Expect(module != nullptr,
           "the Phase 3 executable should expose loadable resources");
    if (module != nullptr) {
        const std::string manifest = ReadEmbeddedManifest(module);
        Expect(!manifest.empty(),
               "the PE image should contain an embedded application manifest");
        Expect(manifest.find("PerMonitorV2") != std::string::npos &&
                   manifest.find("dpiAwareness") != std::string::npos,
               "the embedded manifest should declare Per-Monitor V2 DPI awareness");
        Expect(manifest.find("asInvoker") != std::string::npos,
               "the embedded manifest should retain non-elevated execution");
        FreeLibrary(module);
    }

    if (failures != 0) {
        std::cerr << failures << " manifest/PE test(s) failed.\n";
        return 1;
    }
    std::cout << "All manifest/PE tests passed.\n";
    return 0;
}
