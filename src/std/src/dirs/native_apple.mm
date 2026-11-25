#include "native_apple.hpp"
#include <Foundation/Foundation.h>

asm(".linker_option \"-framework\", \"Foundation\"");

namespace rstd::dirs::sys
{

auto query_apple_roots(void* context, AppleRootReceiver receive) -> bool {
    @autoreleasepool {
        [[unsafe]] {
            NSString* home = NSHomeDirectory();
            if (home != nil &&
                ! receive(context, AppleDirectory::Home, home.fileSystemRepresentation))
                return false;

            NSFileManager* manager = [NSFileManager defaultManager];
            NSURL*         support = [[manager URLsForDirectory:NSApplicationSupportDirectory
                                                      inDomains:NSUserDomainMask] firstObject];
            if (support != nil && ! receive(context,
                                            AppleDirectory::ApplicationSupport,
                                            support.fileSystemRepresentation))
                return false;

            NSURL* cache = [[manager URLsForDirectory:NSCachesDirectory
                                            inDomains:NSUserDomainMask] firstObject];
            if (cache != nil &&
                ! receive(context, AppleDirectory::Cache, cache.fileSystemRepresentation))
                return false;

            for (NSURL* directory in [manager URLsForDirectory:NSApplicationSupportDirectory
                                                     inDomains:NSLocalDomainMask]) {
                if (! receive(context,
                              AppleDirectory::LocalApplicationSupport,
                              directory.fileSystemRepresentation))
                    return false;
            }
            return true;
        }
    }
}

} // namespace rstd::dirs::sys
