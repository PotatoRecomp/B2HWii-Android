#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "Basara 2 Heroes Wii",
    };
    return descriptor;
}

} // namespace RuntimeProduct
