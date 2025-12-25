#if defined(SHOWDUINO_NODE_SUE)
#include "../sue/src/main.cpp"
#elif defined(SHOWDUINO_NODE_IAN)
#include "../ian/src/main.cpp"
#elif defined(SHOWDUINO_NODE_KIDS)
#include "../kids/src/main.cpp"
#elif defined(SHOWDUINO_NODE_UI)
#include "../ui/src/main.cpp"
#else
#error "No node type defined. Define one of: SHOWDUINO_NODE_UI / SHOWDUINO_NODE_SUE / SHOWDUINO_NODE_IAN / SHOWDUINO_NODE_KIDS"
#endif

