#include "animator.h"

void Animator::play(const Model *model, const int clip_index) {
    model_ = model;
    current_clip_ = &model->animations().at(clip_index);
    current_time_ = 0.0;
    bone_matrices_.assign(kMaxBones, glm::mat4(1.0f));
}

void Animator::update(const double dt) {
    if (!model_ || !current_clip_) return;

    current_time_ += current_clip_->ticks_per_second_ * dt;
    current_time_ = std::fmod(current_time_, current_clip_->duration_);

    const Skeleton &skeleton = model_->skeleton();
    calculate_bone_transform(skeleton.nodes_.at(skeleton.root_), glm::mat4(1.0f));
}

void Animator::calculate_bone_transform(const SkeletonNode &node, const glm::mat4 &parent_transform) {
    const Skeleton &skeleton = model_->skeleton();

    glm::mat4 node_transform = node.local_transform_;

    if (const auto it = current_clip_->channels_.find(node.name_); it != current_clip_->channels_.end()) {
        const NodeAnimation &channel = it->second;
        const glm::vec3 pos = interpolate_position(channel, current_time_);
        const glm::quat rot = interpolate_rotation(channel, current_time_);
        const glm::vec3 scale = interpolate_scale(channel, current_time_);

        node_transform = glm::translate(glm::mat4(1.0f), pos)
                         * glm::mat4_cast(rot)
                         * glm::scale(glm::mat4(1.0f), scale);
    }

    const glm::mat4 global_transform = parent_transform * node_transform;

    if (const auto it = skeleton.bone_info_map_.find(node.name_); it != skeleton.bone_info_map_.end()) {
        const int bone_id = it->second.id_;
        bone_matrices_[bone_id] = skeleton.global_inverse_transform_ * global_transform * it->second.offset_;
    }

    for (const int child_index: node.children_) {
        calculate_bone_transform(skeleton.nodes_.at(child_index), global_transform);
    }
}

glm::vec3 Animator::interpolate_position(const NodeAnimation &channel, double time) const {
    const auto &keys = channel.positions_;
    if (keys.empty()) return glm::vec3(0.0f);
    if (keys.size() == 1) return keys[0].value_;

    size_t i = 0;
    while (i < keys.size() - 1 && time > keys[i + 1].time_) ++i;
    const size_t next = std::min(i + 1, keys.size() - 1);

    const double t0 = keys[i].time_, t1 = keys[next].time_;
    const float factor = (t1 > t0) ? static_cast<float>((time - t0) / (t1 - t0)) : 0.0f;
    return glm::mix(keys[i].value_, keys[next].value_, factor);
}

glm::quat Animator::interpolate_rotation(const NodeAnimation &channel, const double time) const {
    const auto &keys = channel.rotations_;
    if (keys.empty()) return glm::quat(1, 0, 0, 0);
    if (keys.size() == 1) return keys[0].value_;

    size_t i = 0;
    while (i < keys.size() - 1 && time > keys[i + 1].time_) ++i;
    const size_t next = std::min(i + 1, keys.size() - 1);

    const double t0 = keys[i].time_, t1 = keys[next].time_;
    const float factor = (t1 > t0) ? static_cast<float>((time - t0) / (t1 - t0)) : 0.0f;
    return glm::normalize(glm::slerp(keys[i].value_, keys[next].value_, factor));
}

glm::vec3 Animator::interpolate_scale(const NodeAnimation &channel, const double time) const {
    const auto &keys = channel.scales_;
    if (keys.empty()) return glm::vec3(1.0f);
    if (keys.size() == 1) return keys[0].value_;

    size_t i = 0;
    while (i < keys.size() - 1 && time > keys[i + 1].time_) ++i;
    const size_t next = std::min(i + 1, keys.size() - 1);

    const double t0 = keys[i].time_, t1 = keys[next].time_;
    const float factor = (t1 > t0) ? static_cast<float>((time - t0) / (t1 - t0)) : 0.0f;
    return glm::mix(keys[i].value_, keys[next].value_, factor);
}
