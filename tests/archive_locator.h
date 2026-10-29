#pragma once

#include "check.h"

#include <servicelocator/service_locator.h>

class ArchiveLocator : public servicelocator::ServiceLocator<ArchiveLocator, 8> {};

class ArchivedService : public servicelocator::Service {
public:
    explicit ArchivedService(ArchiveLocator&) {}
};
