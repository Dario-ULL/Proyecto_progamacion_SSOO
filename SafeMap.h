#include <string_view>
#include <sys/mman.h>
#include <stdexcept>

#include <string_view>
#include <sys/mman.h>  // Para el manejo de mmap

class SafeMap {
 public:
  SafeMap() : sv_(), mapped_memory_(nullptr), size_(0) {}

  SafeMap(std::string_view sv, void* mapped_memory, size_t size)
      : sv_(sv), mapped_memory_(mapped_memory), size_(size) {}

  // Método para obtener el contenido mapeado como un string_view
  std::string_view get() const {
    return sv_;
  }

 private:
  std::string_view sv_;
  void* mapped_memory_;
  size_t size_;
};
