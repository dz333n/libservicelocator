#pragma once

#include "check.h"

#include <servicelocator/service_locator.h>

class ArchiveLocator : public servicelocator::ServiceLocator<ArchiveLocator, 8> {
public:
    SL_FAIL_ON_LOCATOR_ERROR
};

class ArchivedService : public servicelocator::Service {
public:
    explicit ArchivedService(ArchiveLocator&) {}
};
