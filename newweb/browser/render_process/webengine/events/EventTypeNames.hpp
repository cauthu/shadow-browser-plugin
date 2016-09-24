#ifndef EventTypeNames_hpp
#define EventTypeNames_hpp

#include <string>

namespace blink {

namespace EventTypeNames {

extern const std::string load;
extern const std::string DOMContentLoaded;


    bool is_valid(const std::string& name);

}

} // namespace blink

#endif // EventTypeNames_hpp
