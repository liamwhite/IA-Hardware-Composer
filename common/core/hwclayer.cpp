/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <hwclayer.h>

#include <cmath>

#include <hwcutils.h>

namespace hwcomposer {

HwcLayer::~HwcLayer() {
  if (release_fd_ > 0) {
    close(release_fd_);
  }

  if (acquire_fence_ > 0) {
    close(acquire_fence_);
  }
}

void HwcLayer::SetNativeHandle(HWCNativeHandle handle) {
  sf_handle_ = handle;
}

void HwcLayer::SetTransform(int32_t transform) {
  if (transform != transform_) {
    layer_cache_ |= kLayerAttributesChanged;
    transform_ = transform;
  }
}

void HwcLayer::SetAlpha(uint8_t alpha) {
  if (alpha_ != alpha) {
    alpha_ = alpha;
  }
}

void HwcLayer::SetBlending(HWCBlending blending) {
  if (blending != blending_) {
    blending_ = blending;
  }
}

void HwcLayer::SetSourceCrop(const HwcRect<float>& source_crop) {
  if ((source_crop.left != source_crop_.left) ||
      (source_crop.right != source_crop_.right) ||
      (source_crop.top != source_crop_.top) ||
      (source_crop.bottom != source_crop_.bottom)) {
    layer_cache_ |= kSourceRectChanged;
    source_crop_ = source_crop;
    source_crop_width_ =
        static_cast<int>(ceilf(source_crop.right - source_crop.left));
    source_crop_height_ =
        static_cast<int>(ceilf(source_crop.bottom - source_crop.top));
  }
}

void HwcLayer::SetDisplayFrame(const HwcRect<int>& display_frame,
                               uint32_t translate_x_pos) {
  HwcRect<int> frame = display_frame;
  frame.left += translate_x_pos;
  frame.right += translate_x_pos;

  if ((frame.left != display_frame_.left) ||
      (frame.right != display_frame_.right) ||
      (frame.top != display_frame_.top) ||
      (frame.bottom != display_frame_.bottom)) {
    layer_cache_ |= kDisplayFrameRectChanged;
    display_frame_ = frame;
    display_frame_width_ = display_frame_.right - display_frame_.left;
    display_frame_height_ = display_frame_.bottom - display_frame_.top;
  }

  if (!(state_ & kVisibleRegionSet)) {
    visible_rect_ = display_frame_;
  }
}

void HwcLayer::SetSurfaceDamage(const HwcRegion& surface_damage) {
  uint32_t rects = surface_damage.size();
  state_ |= kLayerContentChanged;
  HwcRect<int> rect;
  ResetRectToRegion(surface_damage, rect);
  if (rects == 1) {
    if ((rect.top == 0) && (rect.bottom == 0) && (rect.left == 0) &&
        (rect.right == 0)) {
      state_ &= ~kLayerContentChanged;
      state_ &= ~kSurfaceDamageChanged;
      surface_damage_ = rect;
      return;
    }
  } else if (rects == 0) {
    rect = display_frame_;
  }

  if ((surface_damage_.left == rect.left) &&
      (surface_damage_.top == rect.top) &&
      (surface_damage_.right == rect.right) &&
      (surface_damage_.bottom == rect.bottom)) {
    return;
  }

  state_ |= kSurfaceDamageChanged;
  surface_damage_ = rect;
}

void HwcLayer::SetVisibleRegion(const HwcRegion& visible_region) {
  uint32_t rects = visible_region.size();
  const HwcRect<int>& new_region = visible_region.at(0);
  HwcRect<int> new_visible_rect = new_region;
  state_ |= kVisibleRegionSet;
  state_ &= ~kVisibleRegionChanged;

  for (uint32_t r = 1; r < rects; r++) {
    const HwcRect<int>& rect = visible_region.at(r);
    new_visible_rect.left = std::min(new_region.left, rect.left);
    new_visible_rect.top = std::min(new_region.top, rect.top);
    new_visible_rect.right = std::max(new_region.right, rect.right);
    new_visible_rect.bottom = std::max(new_region.bottom, rect.bottom);
  }

  if ((visible_rect_.left == new_visible_rect.left) &&
      (visible_rect_.top == new_visible_rect.top) &&
      (visible_rect_.right == new_visible_rect.right) &&
      (visible_rect_.bottom == new_visible_rect.bottom)) {
    return;
  }

  state_ |= kVisibleRegionChanged;
  visible_rect_ = new_visible_rect;

  if ((visible_rect_.top == 0) && (visible_rect_.bottom == 0) &&
      (visible_rect_.left == 0) && (visible_rect_.right == 0)) {
    state_ &= ~kVisible;
  } else {
    state_ |= kVisible;
  }
}

void HwcLayer::SetReleaseFence(int32_t fd) {
  if (release_fd_ > 0) {
    close(release_fd_);
    release_fd_ = -1;
  }

  release_fd_ = fd;
}

int32_t HwcLayer::GetReleaseFence() {
  int32_t old_fd = release_fd_;
  release_fd_ = -1;
  return old_fd;
}

void HwcLayer::SetAcquireFence(int32_t fd) {
  if (acquire_fence_ > 0) {
    close(acquire_fence_);
    acquire_fence_ = -1;
  }

  acquire_fence_ = fd;
}

int32_t HwcLayer::GetAcquireFence() {
  int32_t old_fd = acquire_fence_;
  acquire_fence_ = -1;
  return old_fd;
}

void HwcLayer::Validate() {
  state_ &= ~kVisibleRegionChanged;
  state_ |= kLayerValidated;
  state_ &= ~kLayerContentChanged;
  state_ &= ~kSurfaceDamageChanged;
  layer_cache_ &= ~kLayerAttributesChanged;
  layer_cache_ &= ~kDisplayFrameRectChanged;
  layer_cache_ &= ~kSourceRectChanged;
  if (left_constraint_.empty() && left_source_constraint_.empty())
    return;

  if (!left_constraint_.empty()) {
    std::vector<int32_t>().swap(left_constraint_);
  }

  if (!right_constraint_.empty()) {
    std::vector<int32_t>().swap(right_constraint_);
  }

  if (!left_source_constraint_.empty()) {
    std::vector<int32_t>().swap(left_source_constraint_);
  }

  if (!right_source_constraint_.empty()) {
    std::vector<int32_t>().swap(right_source_constraint_);
  }
}

void HwcLayer::SetLayerZOrder(uint32_t order) {
  if (z_order_ != order) {
    z_order_ = order;
  }
}

void HwcLayer::SetLeftConstraint(int32_t left_constraint) {
  left_constraint_.emplace_back(left_constraint);
}

void HwcLayer::SetRightConstraint(int32_t right_constraint) {
  right_constraint_.emplace_back(right_constraint);
}

int32_t HwcLayer::GetLeftConstraint() {
  size_t total = left_constraint_.size();
  if (total == 0)
    return -1;

  if (total == 1)
    return left_constraint_.at(0);

    std::vector<int32_t> temp;
  for (size_t i = 1; i < total; i++) {
    temp.emplace_back(left_constraint_.at(i));
  }

  uint32_t value = left_constraint_.at(0);
  left_constraint_.swap(temp);
  return value;
}

int32_t HwcLayer::GetRightConstraint() {
    size_t total = right_constraint_.size();
    if (total == 0)
      return -1;

    if (total == 1)
      return right_constraint_.at(0);

      std::vector<int32_t> temp;
    for (size_t i = 1; i < total; i++) {
      temp.emplace_back(right_constraint_.at(i));
    }

    uint32_t value = right_constraint_.at(0);
    right_constraint_.swap(temp);
    return value;
}

void HwcLayer::SetLeftSourceConstraint(int32_t left_constraint) {
  left_source_constraint_.emplace_back(left_constraint);
}

void HwcLayer::SetRightSourceConstraint(int32_t right_constraint) {
  right_source_constraint_.emplace_back(right_constraint);
}

int32_t HwcLayer::GetLeftSourceConstraint() {
  size_t total = left_source_constraint_.size();
  if (total == 0)
    return -1;

  if (total == 1)
    return left_source_constraint_.at(0);

  std::vector<int32_t> temp;
  for (size_t i = 1; i < total; i++) {
    temp.emplace_back(left_source_constraint_.at(i));
  }

  uint32_t value = left_source_constraint_.at(0);
  left_source_constraint_.swap(temp);
  return value;
}

int32_t HwcLayer::GetRightSourceConstraint() {
  size_t total = right_source_constraint_.size();
  if (total == 0)
    return -1;

  if (total == 1)
    return right_source_constraint_.at(0);

  std::vector<int32_t> temp;
  for (size_t i = 1; i < total; i++) {
    temp.emplace_back(right_source_constraint_.at(i));
  }

  uint32_t value = right_source_constraint_.at(0);
  right_source_constraint_.swap(temp);
  return value;
}

}  // namespace hwcomposer
