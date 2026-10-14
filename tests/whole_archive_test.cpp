#include "archive_locator.h"

int main() {
    ArchiveLocator locator;
    locator.Bootstrap();
    SL_CHECK(locator.TryGet<ArchivedService>() != nullptr);
    std::printf("whole_archive_test passed\n");
    return 0;
}
