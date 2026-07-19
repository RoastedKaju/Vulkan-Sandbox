#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "model.h"

constexpr uint32_t kMaxBones = 128;

class Animator {
public:
    void play(const Model *model, int clip_index = 0);

    void update(double dt);

    const std::vector<glm::mat4> &bone_matrices() const { return bone_matrices_; }

private:
    void calculate_bone_transform(const SkeletonNode &node, const glm::mat4 &parent_transform);

    glm::vec3 interpolate_position(const NodeAnimation &channel, double time) const;

    glm::quat interpolate_rotation(const NodeAnimation &channel, double time) const;

    glm::vec3 interpolate_scale(const NodeAnimation &channel, double time) const;

    const Model *model_ = nullptr;
    const AnimationClip *current_clip_ = nullptr;
    double current_time_ = 0.0;

    std::vector<glm::mat4> bone_matrices_;
};
