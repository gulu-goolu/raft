#include "utils.h"

void RefCounted::inc_ref() {
    ++ref_count_;
}

void RefCounted::dec_ref() {
    if (--ref_count_ == 0) {
        delete this;
    }
}
