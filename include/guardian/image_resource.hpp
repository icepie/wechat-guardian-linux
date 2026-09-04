#pragma once

#include <string>

namespace guardian {

// Upgrades a live WeChat 4.1.1.8 image resource from thumbnail/mid-image to
// full image without changing the size or ownership of its libc++ strings.
bool upgrade_image_resource(void* resource, std::string& upgraded_path);

} // namespace guardian
