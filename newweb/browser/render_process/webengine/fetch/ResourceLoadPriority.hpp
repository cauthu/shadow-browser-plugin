#ifndef ResourceLoadPriority_hpp
#define ResourceLoadPriority_hpp

namespace blink {

enum ResourceLoadPriority : int {
  ResourceLoadPriorityUnresolved = -1,
  ResourceLoadPriorityVeryLow = 0,
  ResourceLoadPriorityLow,
  ResourceLoadPriorityMedium,
  ResourceLoadPriorityHigh,
  ResourceLoadPriorityVeryHigh,
  ResourceLoadPriorityLowest = ResourceLoadPriorityVeryLow,
  ResourceLoadPriorityHighest = ResourceLoadPriorityVeryHigh,
};

} // namespace

#endif /* ResourceLoadPriority_hpp */
