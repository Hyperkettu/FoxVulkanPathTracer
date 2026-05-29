#include "FoxRenderer.h"

// Static member definitions
template <typename T>
std::unique_ptr<T> Fox::Core::Singleton<T>::instance = nullptr;

template <typename T>
std::once_flag Fox::Core::Singleton<T>::initFlag;