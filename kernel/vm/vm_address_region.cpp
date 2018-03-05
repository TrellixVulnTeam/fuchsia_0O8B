// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_address_region.h>

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <pow2.h>
#include <trace.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <zircon/types.h>

#if WITH_LIB_VDSO
#include <lib/vdso.h>
#endif

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

VmAddressRegion::VmAddressRegion(VmAspace& aspace, vaddr_t base, size_t size, uint32_t vmar_flags)
    : VmAddressRegionOrMapping(base, size, vmar_flags | VMAR_CAN_RWX_FLAGS,
                               &aspace, nullptr) {

    // We add in CAN_RWX_FLAGS above, since an address space can't usefully
    // contain a process without all of these.

    strlcpy(const_cast<char*>(name_), "root", sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAddressRegion& parent, vaddr_t base, size_t size,
                                 uint32_t vmar_flags, const char* name)
    : VmAddressRegionOrMapping(base, size, vmar_flags, parent.aspace_.get(),
                               &parent) {

    strlcpy(const_cast<char*>(name_), name, sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);
}

VmAddressRegion::VmAddressRegion(VmAspace& kernel_aspace)
    : VmAddressRegion(kernel_aspace, kernel_aspace.base(), kernel_aspace.size(),
                      VMAR_FLAG_CAN_MAP_SPECIFIC) {

    // Activate the kernel root aspace immediately
    state_ = LifeCycleState::ALIVE;
}

VmAddressRegion::VmAddressRegion()
    : VmAddressRegionOrMapping(0, 0, 0, nullptr, nullptr) {

    strlcpy(const_cast<char*>(name_), "dummy", sizeof(name_));
    LTRACEF("%p '%s'\n", this, name_);
}

zx_status_t VmAddressRegion::CreateRoot(VmAspace& aspace, uint32_t vmar_flags,
                                        fbl::RefPtr<VmAddressRegion>* out) {
    DEBUG_ASSERT(out);

    fbl::AllocChecker ac;
    auto vmar = new (&ac) VmAddressRegion(aspace, aspace.base(), aspace.size(), vmar_flags);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    vmar->state_ = LifeCycleState::ALIVE;
    *out = fbl::AdoptRef(vmar);
    return ZX_OK;
}

zx_status_t VmAddressRegion::CreateSubVmarInternal(size_t offset, size_t size, uint8_t align_pow2,
                                                   uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo,
                                                   uint64_t vmo_offset, uint arch_mmu_flags,
                                                   const char* name,
                                                   fbl::RefPtr<VmAddressRegionOrMapping>* out) {
    DEBUG_ASSERT(out);

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Check if there are any RWX privileges that the child would have that the
    // parent does not.
    if (vmar_flags & ~flags_ & VMAR_CAN_RWX_FLAGS) {
        return ZX_ERR_ACCESS_DENIED;
    }

    bool is_specific_overwrite = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);
    bool is_specific = static_cast<bool>(vmar_flags & VMAR_FLAG_SPECIFIC) || is_specific_overwrite;
    if (!is_specific && offset != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Check to see if a cache policy exists if a VMO is passed in. VMOs that are not physical
    // return ERR_UNSUPPORTED, anything aside from that and ZX_OK is an error.
    // TODO(cja): explore whether it makes sense to add a default PAGED value to VmObjectPaged
    // and allow them to be treated the same, since by default we're mapping those objects that
    // way anyway.
    if (vmo) {
        uint32_t cache_policy;
        zx_status_t status = vmo->GetMappingCachePolicy(&cache_policy);
        if (status == ZX_OK) {
            // Warn in the event that we somehow receive a VMO that has a cache
            // policy set while also holding cache policy flags within the arch
            // flags. The only path that should be able to achieve this is if
            // something in the kernel maps into their aspace incorrectly.
            if ((arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) != 0) {
                TRACEF("warning: mapping %s has conflicting cache policies: vmo %02x "
                       "arch_mmu_flags %02x.\n",
                       name, cache_policy, arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK);
            }
            arch_mmu_flags |= cache_policy;
        } else if (status != ZX_ERR_NOT_SUPPORTED) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // Check that we have the required privileges if we want a SPECIFIC mapping
    if (is_specific && !(flags_ & VMAR_FLAG_CAN_MAP_SPECIFIC)) {
        return ZX_ERR_ACCESS_DENIED;
    }

    if (offset >= size_ || size > size_ - offset) {
        return ZX_ERR_INVALID_ARGS;
    }

    vaddr_t new_base = -1;
    if (is_specific) {
        new_base = base_ + offset;
        if (!IS_PAGE_ALIGNED(new_base)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (align_pow2 > 0 && (new_base & ((1ULL << align_pow2) - 1))) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (!IsRangeAvailableLocked(new_base, size)) {
            if (is_specific_overwrite) {
                return OverwriteVmMapping(new_base, size, vmar_flags,
                                          vmo, vmo_offset, arch_mmu_flags, out);
            }
            return ZX_ERR_NO_MEMORY;
        }
    } else {
        // If we're not mapping to a specific place, search for an opening.
        zx_status_t status = AllocSpotLocked(size, align_pow2, arch_mmu_flags, &new_base);
        if (status != ZX_OK) {
            return status;
        }
    }

// Notice if this is an executable mapping from the vDSO VMO
// before we lose the VMO reference via fbl::move(vmo).
#if WITH_LIB_VDSO
    const bool is_vdso_code = (vmo &&
                               (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) &&
                               VDso::vmo_is_vdso(vmo));
#endif

    fbl::AllocChecker ac;
    fbl::RefPtr<VmAddressRegionOrMapping> vmar;
    if (vmo) {
        vmar = fbl::AdoptRef(new (&ac)
                                 VmMapping(*this, new_base, size, vmar_flags,
                                           fbl::move(vmo), vmo_offset, arch_mmu_flags));
    } else {
        vmar = fbl::AdoptRef(new (&ac)
                                 VmAddressRegion(*this, new_base, size, vmar_flags, name));
    }

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

#if WITH_LIB_VDSO
    if (is_vdso_code) {
        // For an executable mapping of the vDSO, allow only one per process
        // and only for the valid range of the image.
        if (aspace_->vdso_code_mapping_ ||
            !VDso::valid_code_mapping(vmo_offset, size)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        aspace_->vdso_code_mapping_ = fbl::RefPtr<VmMapping>::Downcast(vmar);
    }
#endif

    vmar->Activate();
    *out = fbl::move(vmar);
    return ZX_OK;
}

zx_status_t VmAddressRegion::CreateSubVmar(size_t offset, size_t size, uint8_t align_pow2,
                                           uint32_t vmar_flags, const char* name,
                                           fbl::RefPtr<VmAddressRegion>* out) {
    DEBUG_ASSERT(out);

    if (!IS_PAGE_ALIGNED(size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Check that only allowed flags have been set
    if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_COMPACT | VMAR_CAN_RWX_FLAGS)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmAddressRegionOrMapping> res;
    zx_status_t status = CreateSubVmarInternal(offset, size, align_pow2, vmar_flags, nullptr, 0,
                                               ARCH_MMU_FLAG_INVALID, name, &res);
    if (status != ZX_OK) {
        return status;
    }
    // TODO(teisenbe): optimize this
    *out = res->as_vm_address_region();
    return ZX_OK;
}

zx_status_t VmAddressRegion::CreateVmMapping(size_t mapping_offset, size_t size, uint8_t align_pow2,
                                             uint32_t vmar_flags, fbl::RefPtr<VmObject> vmo,
                                             uint64_t vmo_offset, uint arch_mmu_flags, const char* name,
                                             fbl::RefPtr<VmMapping>* out) {
    DEBUG_ASSERT(out);
    LTRACEF("%p %#zx %#zx %x\n", this, mapping_offset, size, vmar_flags);

    // Check that only allowed flags have been set
    if (vmar_flags & ~(VMAR_FLAG_SPECIFIC | VMAR_FLAG_SPECIFIC_OVERWRITE | VMAR_CAN_RWX_FLAGS)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Validate that arch_mmu_flags does not contain any prohibited flags
    if (!is_valid_mapping_flags(arch_mmu_flags)) {
        return ZX_ERR_ACCESS_DENIED;
    }

    // If size overflows, it'll become 0 and get rejected in
    // CreateSubVmarInternal.
    size = ROUNDUP(size, PAGE_SIZE);

    // Make sure that vmo_offset is aligned and that a mapping of this size
    // wouldn't overflow the vmo offset.
    if (!IS_PAGE_ALIGNED(vmo_offset) || vmo_offset + size < vmo_offset) {
        return ZX_ERR_INVALID_ARGS;
    }

    // If we're mapping it with a specific permission, we should allow
    // future Protect() calls on the mapping to keep that permission.
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_READ;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_WRITE;
    }
    if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
        vmar_flags |= VMAR_FLAG_CAN_MAP_EXECUTE;
    }

    fbl::RefPtr<VmAddressRegionOrMapping> res;
    zx_status_t status =
        CreateSubVmarInternal(mapping_offset, size, align_pow2, vmar_flags, fbl::move(vmo),
                              vmo_offset, arch_mmu_flags, name, &res);
    if (status != ZX_OK) {
        return status;
    }
    // TODO(teisenbe): optimize this
    *out = res->as_vm_mapping();
    return ZX_OK;
}

zx_status_t VmAddressRegion::OverwriteVmMapping(
    vaddr_t base, size_t size, uint32_t vmar_flags,
    fbl::RefPtr<VmObject> vmo, uint64_t vmo_offset,
    uint arch_mmu_flags, fbl::RefPtr<VmAddressRegionOrMapping>* out) {

    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(vmo);
    DEBUG_ASSERT(vmar_flags & VMAR_FLAG_SPECIFIC_OVERWRITE);

    fbl::AllocChecker ac;
    fbl::RefPtr<VmAddressRegionOrMapping> vmar;
    vmar = fbl::AdoptRef(new (&ac)
                             VmMapping(*this, base, size, vmar_flags,
                                       fbl::move(vmo), vmo_offset, arch_mmu_flags));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = UnmapInternalLocked(base, size, false /* can_destroy_regions */);
    if (status != ZX_OK) {
        return status;
    }

    vmar->Activate();
    *out = fbl::move(vmar);
    return ZX_OK;
}

zx_status_t VmAddressRegion::DestroyLocked() {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    LTRACEF("%p '%s'\n", this, name_);

    // Take a reference to ourself, so that we do not get destructed after
    // dropping our last reference in this method (e.g. when calling
    // subregions_.erase below).
    fbl::RefPtr<VmAddressRegion> self(this);

    while (!subregions_.is_empty()) {
        fbl::RefPtr<VmAddressRegionOrMapping> child(&subregions_.front());

        // DestroyLocked should remove this child from our list on success
        zx_status_t status = child->DestroyLocked();
        if (status != ZX_OK) {
            // TODO(teisenbe): Do we want to handle this case differently?
            return status;
        }
    }

    // Detach the now dead region from the parent
    if (parent_) {
        DEBUG_ASSERT(subregion_list_node_.InContainer());
        parent_->RemoveSubregion(this);
    }

    parent_ = nullptr;
    state_ = LifeCycleState::DEAD;
    return ZX_OK;
}

void VmAddressRegion::RemoveSubregion(VmAddressRegionOrMapping* region) {
    subregions_.erase(*region);
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegion(vaddr_t addr) {
    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return nullptr;
    }
    return FindRegionLocked(addr);
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAddressRegion::FindRegionLocked(vaddr_t addr) {
    canary_.Assert();

    // Find the first region with a base greather than *addr*.  If a region
    // exists for *addr*, it will be immediately before it.
    auto itr = --subregions_.upper_bound(addr);
    if (!itr.IsValid() || itr->base() > addr || addr > itr->base() + itr->size() - 1) {
        return nullptr;
    }

    return fbl::RefPtr<VmAddressRegionOrMapping>(&*itr);
}

size_t VmAddressRegion::AllocatedPagesLocked() const {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    if (state_ != LifeCycleState::ALIVE) {
        return 0;
    }

    size_t sum = 0;
    for (const auto& child : subregions_) {
        sum += child.AllocatedPagesLocked();
    }
    return sum;
}

zx_status_t VmAddressRegion::PageFault(vaddr_t va, uint pf_flags) {
    canary_.Assert();
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    for (auto vmar = WrapRefPtr(this);
         auto next = vmar->FindRegionLocked(va);
         vmar = next->as_vm_address_region()) {
        if (next->is_mapping())
            return next->PageFault(va, pf_flags);
    }

    return ZX_ERR_NOT_FOUND;
}

bool VmAddressRegion::IsRangeAvailableLocked(vaddr_t base, size_t size) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(size > 0);

    // Find the first region with base > *base*.  Since subregions_ has no
    // overlapping elements, we just need to check this one and the prior
    // child.

    auto prev = subregions_.upper_bound(base);
    auto next = prev--;

    if (prev.IsValid()) {
        vaddr_t prev_last_byte;
        if (add_overflow(prev->base(), prev->size() - 1, &prev_last_byte)) {
            return false;
        }
        if (prev_last_byte >= base) {
            return false;
        }
    }

    if (next.IsValid() && next != subregions_.end()) {
        vaddr_t last_byte;
        if (add_overflow(base, size - 1, &last_byte)) {
            return false;
        }
        if (next->base() <= last_byte) {
            return false;
        }
    }
    return true;
}

bool VmAddressRegion::CheckGapLocked(const ChildList::iterator& prev,
                                     const ChildList::iterator& next,
                                     vaddr_t* pva, vaddr_t search_base, vaddr_t align,
                                     size_t region_size, size_t min_gap, uint arch_mmu_flags) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    vaddr_t gap_beg; // first byte of a gap
    vaddr_t gap_end; // last byte of a gap

    uint prev_arch_mmu_flags;
    uint next_arch_mmu_flags;

    DEBUG_ASSERT(pva);

    // compute the starting address of the gap
    if (prev.IsValid()) {
        if (add_overflow(prev->base(),prev->size(), &gap_beg) ||
            add_overflow(gap_beg, min_gap, &gap_beg)) {
            goto not_found;
        }
    } else {
        gap_beg = base_;
    }

    // compute the ending address of the gap
    if (next.IsValid()) {
        if (gap_beg == next->base())
            goto next_gap; // no gap between regions
        if (sub_overflow(next->base(), 1, &gap_end) ||
            sub_overflow(gap_end, min_gap, &gap_end)) {
            goto not_found;
        }
    } else {
        if (gap_beg == base_ + size_) {
            goto not_found; // no gap at the end of address space. Stop search
        }
        if (add_overflow(base_, size_ - 1, &gap_end)) {
            goto not_found;
        }
    }

    DEBUG_ASSERT(gap_end > gap_beg);

    // trim it to the search range
    if (gap_end <= search_base)
        return false;
    if (gap_beg < search_base)
        gap_beg = search_base;

    DEBUG_ASSERT(gap_end > gap_beg);

    LTRACEF_LEVEL(2, "search base %#" PRIxPTR " gap_beg %#" PRIxPTR " end %#" PRIxPTR "\n",
                  search_base, gap_beg, gap_end);

    prev_arch_mmu_flags = (prev.IsValid() && prev->is_mapping())
                              ? prev->as_vm_mapping()->arch_mmu_flags()
                              : ARCH_MMU_FLAG_INVALID;

    next_arch_mmu_flags = (next.IsValid() && next->is_mapping())
                              ? next->as_vm_mapping()->arch_mmu_flags()
                              : ARCH_MMU_FLAG_INVALID;

    *pva = aspace_->arch_aspace().PickSpot(gap_beg, prev_arch_mmu_flags, gap_end,
                                           next_arch_mmu_flags, align, region_size, arch_mmu_flags);
    if (*pva < gap_beg)
        goto not_found; // address wrapped around

    if (*pva < gap_end && ((gap_end - *pva + 1) >= region_size)) {
        // we have enough room
        return true; // found spot, stop search
    }

next_gap:
    return false; // continue search

not_found:
    *pva = -1;
    return true; // not_found: stop search
}

zx_status_t VmAddressRegion::AllocSpotLocked(size_t size, uint8_t align_pow2, uint arch_mmu_flags,
                                             vaddr_t* spot) {
    canary_.Assert();
    DEBUG_ASSERT(size > 0 && IS_PAGE_ALIGNED(size));
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    LTRACEF_LEVEL(2, "aspace %p size 0x%zx align %hhu\n", this, size,
                  align_pow2);

    if (aspace_->is_aslr_enabled()) {
        if (flags_ & VMAR_FLAG_COMPACT) {
            return CompactRandomizedRegionAllocatorLocked(size, align_pow2, arch_mmu_flags, spot);
        } else {
            return NonCompactRandomizedRegionAllocatorLocked(size, align_pow2, arch_mmu_flags,
                                                             spot);
        }
    }
    return LinearRegionAllocatorLocked(size, align_pow2, arch_mmu_flags, spot);
}

bool VmAddressRegion::EnumerateChildrenLocked(VmEnumerator* ve, uint depth) {
    canary_.Assert();
    DEBUG_ASSERT(ve != nullptr);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    for (auto& child : subregions_) {
        DEBUG_ASSERT(child.IsAliveLocked());
        if (child.is_mapping()) {
            VmMapping* mapping = child.as_vm_mapping().get();
            DEBUG_ASSERT(mapping != nullptr);
            if (!ve->OnVmMapping(mapping, this, depth)) {
                return false;
            }
        } else {
            VmAddressRegion* vmar = child.as_vm_address_region().get();
            DEBUG_ASSERT(vmar != nullptr);
            if (!ve->OnVmAddressRegion(vmar, depth)) {
                return false;
            }
            if (!vmar->EnumerateChildrenLocked(ve, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

void VmAddressRegion::Dump(uint depth, bool verbose) const {
    canary_.Assert();
    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("vmar %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx ref %d '%s'\n", this,
           base_, base_ + size_ - 1, size_, ref_count_debug(), name_);
    for (const auto& child : subregions_) {
        child.Dump(depth + 1, verbose);
    }
}

void VmAddressRegion::Activate() {
    DEBUG_ASSERT(state_ == LifeCycleState::NOT_READY);
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    state_ = LifeCycleState::ALIVE;
    parent_->subregions_.insert(fbl::RefPtr<VmAddressRegionOrMapping>(this));
}

zx_status_t VmAddressRegion::Unmap(vaddr_t base, size_t size) {
    canary_.Assert();

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0 || !IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    return UnmapInternalLocked(base, size, true /* can_destroy_regions */);
}

zx_status_t VmAddressRegion::UnmapInternalLocked(vaddr_t base, size_t size, bool can_destroy_regions) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    if (!is_in_range(base, size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (subregions_.is_empty()) {
        return ZX_OK;
    }

#if WITH_LIB_VDSO
    // Any unmap spanning the vDSO code mapping is verboten.
    if (aspace_->vdso_code_mapping_ &&
        aspace_->vdso_code_mapping_->base() >= base &&
        aspace_->vdso_code_mapping_->base() - base < size) {
        return ZX_ERR_ACCESS_DENIED;
    }
#endif

    const vaddr_t end_addr = base + size;
    const auto end = subregions_.lower_bound(end_addr);

    // Find the first region with a base greater than *base*.  If a region
    // exists for *base*, it will be immediately before it.
    auto begin = --subregions_.upper_bound(base);
    if (!begin.IsValid()) {
        begin = subregions_.begin();
    } else if (base >= begin->base() + begin->size()) {
        // If *base* isn't in this region, ignore it.
        ++begin;
    }

    // Check if we're partially spanning a subregion, or aren't allowed to
    // destroy regions and are spanning a region, and bail if we are.
    for (auto itr = begin; itr != end; ++itr) {
        const vaddr_t itr_end = itr->base() + itr->size();
        if (!itr->is_mapping() && (!can_destroy_regions ||
                                   itr->base() < base || itr_end > end_addr)) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    for (auto itr = begin; itr != end;) {
        // Create a copy of the iterator, in case we destroy this element
        auto curr = itr++;

        const vaddr_t curr_end = curr->base() + curr->size();
        if (curr->is_mapping()) {
            const vaddr_t unmap_base = fbl::max(curr->base(), base);
            const vaddr_t unmap_end = fbl::min(curr_end, end_addr);
            const size_t unmap_size = unmap_end - unmap_base;

            // If we're unmapping the entire region, just call Destroy
            if (unmap_base == curr->base() && unmap_size == curr->size()) {
                __UNUSED zx_status_t status = curr->as_vm_mapping()->DestroyLocked();
                DEBUG_ASSERT(status == ZX_OK);
                continue;
            }

            // VmMapping::Unmap should only fail if it needs to allocate, which only
            // happens if it is unmapping from the middle of a region.  That can only
            // happen if there is only one region being operated on here, so we
            // can just forward along the error without having to rollback.
            // TODO(teisenbe): Technically arch_mmu_unmap() itself can also
            // fail.  We need to rework the system so that is no longer
            // possible.
            zx_status_t status = curr->as_vm_mapping()->UnmapLocked(unmap_base, unmap_size);
            DEBUG_ASSERT(status == ZX_OK || curr == begin);
            if (status != ZX_OK) {
                return status;
            }
        } else {
            DEBUG_ASSERT(curr->base() >= base && curr_end <= end_addr);
            __UNUSED zx_status_t status = curr->DestroyLocked();
            DEBUG_ASSERT(status == ZX_OK);
        }
    }

    return ZX_OK;
}

zx_status_t VmAddressRegion::Protect(vaddr_t base, size_t size, uint new_arch_mmu_flags) {
    canary_.Assert();

    size = ROUNDUP(size, PAGE_SIZE);
    if (size == 0 || !IS_PAGE_ALIGNED(base)) {
        return ZX_ERR_INVALID_ARGS;
    }

    AutoLock guard(aspace_->lock());
    if (state_ != LifeCycleState::ALIVE) {
        return ZX_ERR_BAD_STATE;
    }

    if (!is_in_range(base, size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (subregions_.is_empty()) {
        return ZX_ERR_NOT_FOUND;
    }

    const vaddr_t end_addr = base + size;
    const auto end = subregions_.lower_bound(end_addr);

    // Find the first region with a base greater than *base*.  If a region
    // exists for *base*, it will be immediately before it.  If *base* isn't in
    // that entry, bail since it's unmapped.
    auto begin = --subregions_.upper_bound(base);
    if (!begin.IsValid() || begin->base() + begin->size() <= base) {
        return ZX_ERR_NOT_FOUND;
    }

    // Check if we're overlapping a subregion, or a part of the range is not
    // mapped, or the new permissions are invalid for some mapping in the range.
    vaddr_t last_mapped = begin->base();
    for (auto itr = begin; itr != end; ++itr) {
        if (!itr->is_mapping()) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (itr->base() != last_mapped) {
            return ZX_ERR_NOT_FOUND;
        }
        if (!itr->is_valid_mapping_flags(new_arch_mmu_flags)) {
            return ZX_ERR_ACCESS_DENIED;
        }
#if WITH_LIB_VDSO
        if (itr->as_vm_mapping() == aspace_->vdso_code_mapping_) {
            return ZX_ERR_ACCESS_DENIED;
        }
#endif

        last_mapped = itr->base() + itr->size();
    }
    if (last_mapped < base + size) {
        return ZX_ERR_NOT_FOUND;
    }

    for (auto itr = begin; itr != end;) {
        DEBUG_ASSERT(itr->is_mapping());

        auto next = itr;
        ++next;

        const vaddr_t curr_end = itr->base() + itr->size();
        const vaddr_t protect_base = fbl::max(itr->base(), base);
        const vaddr_t protect_end = fbl::min(curr_end, end_addr);
        const size_t protect_size = protect_end - protect_base;

        zx_status_t status = itr->as_vm_mapping()->ProtectLocked(protect_base, protect_size,
                                                                 new_arch_mmu_flags);
        if (status != ZX_OK) {
            // TODO(teisenbe): Try to work out a way to guarantee success, or
            // provide a full unwind?
            return status;
        }

        itr = fbl::move(next);
    }

    return ZX_OK;
}

zx_status_t VmAddressRegion::LinearRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                         uint arch_mmu_flags, vaddr_t* spot) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    const vaddr_t base = 0;

    if (align_pow2 < PAGE_SIZE_SHIFT)
        align_pow2 = PAGE_SIZE_SHIFT;
    const vaddr_t align = 1UL << align_pow2;

    // Find the first gap in the address space which can contain a region of the
    // requested size.
    auto before_iter = subregions_.end();
    auto after_iter = subregions_.begin();

    do {
        if (CheckGapLocked(before_iter, after_iter, spot, base, align, size, 0, arch_mmu_flags)) {
            if (*spot != static_cast<vaddr_t>(-1)) {
                return ZX_OK;
            } else {
                return ZX_ERR_NO_MEMORY;
            }
        }

        before_iter = after_iter++;
    } while (before_iter.IsValid());

    // couldn't find anything
    return ZX_ERR_NO_MEMORY;
}

template <typename F>
void VmAddressRegion::ForEachGap(F func, uint8_t align_pow2) {
    const vaddr_t align = 1UL << align_pow2;

    // Scan the regions list to find the gap to the left of each region.  We
    // round up the end of the previous region to the requested alignment, so
    // all gaps reported will be for aligned ranges.
    vaddr_t prev_region_end = ROUNDUP(base_, align);
    for (const auto& region : subregions_) {
        if (region.base() > prev_region_end) {
            const size_t gap = region.base() - prev_region_end;
            if (!func(prev_region_end, gap)) {
                return;
            }
        }
        prev_region_end = ROUNDUP(region.base() + region.size(), align);
    }

    // Grab the gap to the right of the last region (note that if there are no
    // regions, this handles reporting the VMAR's whole span as a gap).
    const vaddr_t end = base_ + size_;
    if (end > prev_region_end) {
        const size_t gap = end - prev_region_end;
        func(prev_region_end, gap);
    }
}

namespace {

// Compute the number of allocation spots that satisfy the alignment within the
// given range size, for a range that has a base that satisfies the alignment.
constexpr size_t AllocationSpotsInRange(size_t range_size, size_t alloc_size, uint8_t align_pow2) {
    return ((range_size - alloc_size) >> align_pow2) + 1;
}

} // namespace {}

// Perform allocations for VMARs that aren't using the COMPACT policy.  This
// allocator works by choosing uniformly at random from the set of positions
// that could satisfy the allocation.
zx_status_t VmAddressRegion::NonCompactRandomizedRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                                       uint arch_mmu_flags,
                                                                       vaddr_t* spot) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));
    DEBUG_ASSERT(spot);

    align_pow2 = fbl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
    const vaddr_t align = 1UL << align_pow2;

    // Calculate the number of spaces that we can fit this allocation in.
    size_t candidate_spaces = 0;
    ForEachGap([align, align_pow2, size, &candidate_spaces](vaddr_t gap_base, size_t gap_len) -> bool {
        DEBUG_ASSERT(IS_ALIGNED(gap_base, align));
        if (gap_len >= size) {
            candidate_spaces += AllocationSpotsInRange(gap_len, size, align_pow2);
        }
        return true;
    },
               align_pow2);

    if (candidate_spaces == 0) {
        return ZX_ERR_NO_MEMORY;
    }

    // Choose the index of the allocation to use.
    size_t selected_index = aspace_->AslrPrng().RandInt(candidate_spaces);
    DEBUG_ASSERT(selected_index < candidate_spaces);

    // Find which allocation we picked.
    vaddr_t alloc_spot = static_cast<vaddr_t>(-1);
    ForEachGap([align_pow2, size, &alloc_spot, &selected_index](vaddr_t gap_base,
                                                                size_t gap_len) -> bool {
        if (gap_len < size) {
            return true;
        }

        const size_t spots = AllocationSpotsInRange(gap_len, size, align_pow2);
        if (selected_index < spots) {
            alloc_spot = gap_base + (selected_index << align_pow2);
            return false;
        }
        selected_index -= spots;
        return true;
    },
               align_pow2);
    ASSERT(alloc_spot != static_cast<vaddr_t>(-1));
    ASSERT(IS_ALIGNED(alloc_spot, align));

    // Sanity check that the allocation fits.
    auto after_iter = subregions_.upper_bound(alloc_spot + size - 1);
    auto before_iter = after_iter;

    if (after_iter == subregions_.begin() || subregions_.size() == 0) {
        before_iter = subregions_.end();
    } else {
        --before_iter;
    }

    ASSERT(before_iter == subregions_.end() || before_iter.IsValid());

    if (CheckGapLocked(before_iter, after_iter, spot, alloc_spot, align, size, 0,
                       arch_mmu_flags) &&
        *spot != static_cast<vaddr_t>(-1)) {
        return ZX_OK;
    }
    panic("Unexpected allocation failure\n");
}

// The COMPACT allocator begins by picking a random offset in the region to
// start allocations at, and then places new allocations to the left and right
// of the original region with small random-length gaps between.
zx_status_t VmAddressRegion::CompactRandomizedRegionAllocatorLocked(size_t size, uint8_t align_pow2,
                                                                    uint arch_mmu_flags,
                                                                    vaddr_t* spot) {
    DEBUG_ASSERT(is_mutex_held(aspace_->lock()));

    align_pow2 = fbl::max(align_pow2, static_cast<uint8_t>(PAGE_SIZE_SHIFT));
    const vaddr_t align = 1UL << align_pow2;

    if (unlikely(subregions_.size() == 0)) {
        return NonCompactRandomizedRegionAllocatorLocked(size, align_pow2, arch_mmu_flags, spot);
    }

    // Decide if we're allocating before or after the existing allocations, and
    // how many gap pages to use.
    bool alloc_before;
    size_t num_gap_pages;
    {
        uint8_t entropy;
        aspace_->AslrPrng().Draw(&entropy, sizeof(entropy));
        alloc_before = entropy & 1;
        num_gap_pages = (entropy >> 1) + 1;
    }

    // Try our first choice for *num_gap_pages*, but if that fails, try fewer
    for (size_t gap_pages = num_gap_pages; gap_pages > 0; gap_pages >>= 1) {
        // Try our first choice for *alloc_before*, but if that fails, try the other
        for (size_t i = 0; i < 2; ++i, alloc_before = !alloc_before) {
            ChildList::iterator before_iter;
            ChildList::iterator after_iter;
            vaddr_t chosen_base;
            if (alloc_before) {
                before_iter = subregions_.end();
                after_iter = subregions_.begin();

                vaddr_t base;
                if (sub_overflow(after_iter->base(), size, &base) ||
                    sub_overflow(base, PAGE_SIZE * gap_pages, &base)) {
                    continue;
                }

                chosen_base = base;
            } else {
                before_iter = --subregions_.end();
                after_iter = subregions_.end();
                DEBUG_ASSERT(before_iter.IsValid());

                vaddr_t base;
                if (add_overflow(before_iter->base(), before_iter->size(), &base) ||
                    add_overflow(base, PAGE_SIZE * gap_pages, &base)) {
                    continue;
                }

                chosen_base = base;
            }

            if (CheckGapLocked(before_iter, after_iter, spot, chosen_base, align, size, 0,
                               arch_mmu_flags) &&
                *spot != static_cast<vaddr_t>(-1)) {
                return ZX_OK;
            }
        }
    }

    return ZX_ERR_NO_MEMORY;
}
