#pragma once

#include "core/dsl.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace core::dsl {

inline std::vector<const Element*> orderedElements(const std::vector<std::unique_ptr<Element>>& elements) {
    std::vector<const Element*> ordered;
    ordered.reserve(elements.size());
    for (const auto& element : elements) {
        ordered.push_back(element.get());
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Element* a, const Element* b) {
        return a->zIndex < b->zIndex;
    });
    return ordered;
}

} // namespace core::dsl
