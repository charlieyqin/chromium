// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_
#define UI_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/compositor/compositor_export.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/transform.h"

namespace ui {

// Layer animations interact with the layers using this interface.
class COMPOSITOR_EXPORT LayerAnimationDelegate {
 public:
  virtual void SetBoundsFromAnimation(const gfx::Rect& bounds) = 0;
  virtual void SetTransformFromAnimation(const Transform& transform) = 0;
  virtual void SetOpacityFromAnimation(float opacity) = 0;
  virtual void SetVisibilityFromAnimation(bool visibility) = 0;
  virtual void SetBrightnessFromAnimation(float brightness) = 0;
  virtual void SetGrayscaleFromAnimation(float grayscale) = 0;
  virtual void SetColorFromAnimation(SkColor color) = 0;
  virtual void ScheduleDrawForAnimation() = 0;
  virtual const gfx::Rect& GetBoundsForAnimation() const = 0;
  virtual const Transform& GetTransformForAnimation() const = 0;
  virtual float GetOpacityForAnimation() const = 0;
  virtual bool GetVisibilityForAnimation() const = 0;
  virtual float GetBrightnessForAnimation() const = 0;
  virtual float GetGrayscaleForAnimation() const = 0;
  virtual SkColor GetColorForAnimation() const = 0;

 protected:
  virtual ~LayerAnimationDelegate() {}
};

}  // namespace ui

#endif  // UI_COMPOSITOR_LAYER_ANIMATION_DELEGATE_H_
