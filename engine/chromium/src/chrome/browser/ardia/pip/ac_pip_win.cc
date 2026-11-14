// Copyright 2026 Ardia. Apache-2.0.

#include "chrome/browser/ardia/pip/ac_pip_native.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/common/chrome_paths.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"

namespace {

constexpr int kMaxBoundsFileSize = 256;

base::FilePath BoundsFile() {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
    return base::FilePath();
  }
  return dir.AppendASCII("ArdiaPictureInPicture.json");
}

scoped_refptr<base::SequencedTaskRunner> BoundsWriter() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return *runner;
}

}

std::optional<gfx::Rect> LoadArdiaPictureInPictureBounds() {
  const base::FilePath file = BoundsFile();
  std::string text;
  if (file.empty() ||
      !base::ReadFileToStringWithMaxSize(file, &text, kMaxBoundsFileSize)) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  const base::ListValue* values = parsed ? parsed->GetIfList() : nullptr;
  if (!values || values->size() != 4) {
    return std::nullopt;
  }
  for (const base::Value& value : *values) {
    if (!value.is_int()) {
      return std::nullopt;
    }
  }
  gfx::Rect bounds((*values)[0].GetInt(), (*values)[1].GetInt(),
                   (*values)[2].GetInt(), (*values)[3].GetInt());
  if (bounds.IsEmpty()) {
    return std::nullopt;
  }
  return bounds;
}

void SaveArdiaPictureInPictureBounds(const gfx::Rect& bounds) {
  static gfx::Rect last;
  const base::FilePath file = BoundsFile();
  if (file.empty() || bounds.IsEmpty() || bounds == last) {
    return;
  }
  last = bounds;
  std::string text;
  base::JSONWriter::Write(base::ListValue()
                              .Append(bounds.x())
                              .Append(bounds.y())
                              .Append(bounds.width())
                              .Append(bounds.height()),
                          &text);
  BoundsWriter()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](const base::FilePath& file, const std::string& text) {
                       base::ImportantFileWriter::WriteFileAtomically(file,
                                                                      text);
                     },
                     file, std::move(text)));
}

void FadeOutArdiaPictureInPicture(gfx::NativeWindow window,
                                  base::TimeDelta duration) {
  ui::Layer* layer = window ? window->layer() : nullptr;
  if (!layer) {
    return;
  }
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(duration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  layer->SetOpacity(0.0f);
}

void ResetArdiaPictureInPictureFade(gfx::NativeWindow window) {
  ui::Layer* layer = window ? window->layer() : nullptr;
  if (!layer || (layer->GetTargetOpacity() == 1.0f &&
                 !layer->GetAnimator()->is_animating())) {
    return;
  }
  layer->GetAnimator()->StopAnimating();
  layer->SetOpacity(1.0f);
}

bool PresentArdiaPictureInPictureControls(
    gfx::NativeWindow,
    const std::vector<ArdiaPipNativeControl>&) {
  return false;
}
