#pragma once

#include <Eigen/Dense>
#include <Eigen/StdVector>
#include <robot_design/types.h>
#include <vector>

namespace robot_design {

enum class JointType : Index { FREE, HINGE, FIXED };

struct Link {
  Link(Index parent, JointType joint_type, Scalar joint_pos,
       const Quaternion &joint_rot, const Vector3 &joint_axis, Scalar length)
      : parent_(parent), joint_type_(joint_type), joint_pos_(joint_pos),
        joint_rot_(joint_rot), joint_axis_(joint_axis), length_(length) {}

  // Parent link index (-1 for base link)
  Index parent_;
  // Joint type
  JointType joint_type_;
  // Joint position on parent link (0 = beginning, 1 = end)
  Scalar joint_pos_;
  // Joint rotation relative to parent link
  Quaternion joint_rot_;
  // Joint axis relative to the joint frame (defined by previous 3 parameters)
  Vector3 joint_axis_;
  // Link length
  Scalar length_;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

struct Robot {
  Robot(Scalar link_density, Scalar link_radius, Scalar friction)
      : link_density_(link_density), link_radius_(link_radius),
        friction_(friction), links_() {}
  Scalar link_density_;  // Mass of links per unit of length
  Scalar link_radius_;
  Scalar friction_;
  std::vector<Link, Eigen::aligned_allocator<Link>> links_;
};

}  // namespace drbs