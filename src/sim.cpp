#include <robot_design/sim.h>
#include <robot_design/utils.h>
#include <Serialize/BulletWorldImporter/btMultiBodyWorldImporter.h>

namespace robot_design {

BulletSimulation::BulletSimulation() {
  collision_config_ = std::make_shared<btDefaultCollisionConfiguration>();
  dispatcher_ = std::make_shared<btCollisionDispatcher>(collision_config_.get());
  pair_cache_ = std::make_shared<btHashedOverlappingPairCache>();
  broadphase_ = std::make_shared<btDbvtBroadphase>(pair_cache_.get());
  solver_ = std::make_shared<btMultiBodyConstraintSolver>();
  world_ = std::make_shared<btMultiBodyDynamicsWorld>(dispatcher_.get(),
      broadphase_.get(), solver_.get(), collision_config_.get());
  world_->setGravity(btVector3(0, -9.81, 0));
  world_->getDispatchInfo().m_deterministicOverlappingPairs = true;
}

BulletSimulation::~BulletSimulation() {
  for (auto &robot_wrapper : robot_wrappers_) {
    unregisterRobotWrapper(robot_wrapper);
  }
  for (auto &prop_wrapper : prop_wrappers_) {
    unregisterPropWrapper(prop_wrapper);
  }
}

Index BulletSimulation::addRobot(std::shared_ptr<const Robot> robot,
                                 const Vector3 &pos, const Quaternion &rot) {
  robot_wrappers_.emplace_back(robot);
  BulletRobotWrapper &wrapper = robot_wrappers_.back();
  wrapper.col_shapes_.resize(robot->links_.size());

  for (int i = 0; i < robot->links_.size(); ++i) {
    const Link &link = robot->links_[i];

    auto col_shape = std::make_shared<btCapsuleShapeX>(robot->link_radius_,
        link.length_);
    Scalar link_mass = link.length_ * robot->link_density_;
    btVector3 link_inertia;
    col_shape->calculateLocalInertia(link_mass, link_inertia);

    if (i == 0) {
      assert(link.parent_ == -1 && link.joint_type_ == JointType::FREE);
      wrapper.multi_body_ = std::make_shared<btMultiBody>(
          /*n_links=*/robot->links_.size() - 1,
          /*mass=*/link_mass,
          /*inertia=*/link_inertia,
          /*fixedBase=*/false,
          /*canSleep=*/false);
      wrapper.multi_body_->setBaseWorldTransform(btTransform(
          /*q=*/bulletQuaternionFromEigen(rot),
          /*c=*/bulletVector3FromEigen(pos)));
    } else {
      btQuaternion joint_rot = bulletQuaternionFromEigen(link.joint_rot_);
      btVector3 joint_axis = bulletVector3FromEigen(link.joint_axis_);
      btVector3 joint_offset((link.joint_pos_ - 0.5) * robot->links_[link.parent_].length_, 0, 0);
      btVector3 com_offset(0.5 * link.length_, 0, 0);
      switch (link.joint_type_) {
      case JointType::HINGE:
        wrapper.multi_body_->setupRevolute(
          /*i=*/i - 1,  // Base link is already accounted for
          /*mass=*/link_mass,
          /*inertia=*/link_inertia,
          /*parent=*/link.parent_ - 1,
          /*rotParentToThis=*/joint_rot,
          /*joint_axis=*/joint_axis,
          /*parentComToThisPivotOffset=*/joint_offset,
          /*thisPivotToThisComOffset=*/com_offset,
          /*disableParentCollision=*/true);
        break;
      case JointType::FIXED:
        wrapper.multi_body_->setupFixed(
          /*i=*/i - 1,  // Base link is already accounted for
          /*mass=*/link_mass,
          /*inertia=*/link_inertia,
          /*parent=*/link.parent_ - 1,
          /*rotParentToThis=*/joint_rot,
          /*parentComToThisPivotOffset=*/joint_offset,
          /*thisPivotToThisComOffset=*/com_offset);
        break;
      }
    }

    wrapper.col_shapes_[i] = std::move(col_shape);
  }

  wrapper.multi_body_->finalizeMultiDof();
  world_->addMultiBody(wrapper.multi_body_.get());
  wrapper.multi_body_->setLinearDamping(0.0);
  wrapper.multi_body_->setAngularDamping(0.0);

  // Add collision objects to world
  wrapper.colliders_.resize(wrapper.col_shapes_.size());
  for (int i = 0; i < wrapper.col_shapes_.size(); ++i) {
    auto collider = std::make_shared<btMultiBodyLinkCollider>(
        wrapper.multi_body_.get(), i - 1);
    collider->setCollisionShape(wrapper.col_shapes_[i].get());
    collider->setFriction(robot->friction_);
    world_->addCollisionObject(collider.get(),
                               /*collisionFilterGroup=*/1,
                               /*collisionFilterMask=*/2);
    if (i == 0) {
      wrapper.multi_body_->setBaseCollider(collider.get());
    } else {
      wrapper.multi_body_->getLink(i - 1).m_collider = collider.get();
    }
    wrapper.colliders_[i] = std::move(collider);
  }

  // Initialize collision object world transforms
  btAlignedObjectArray<btQuaternion> scratch_q;
  btAlignedObjectArray<btVector3> scratch_m;
  wrapper.multi_body_->forwardKinematics(scratch_q, scratch_m);
  btAlignedObjectArray<btQuaternion> world_to_local;
  btAlignedObjectArray<btVector3> local_origin;
  wrapper.multi_body_->updateCollisionObjectWorldTransforms(world_to_local, local_origin);

  return robot_wrappers_.size() - 1;
}

Index BulletSimulation::addProp(std::shared_ptr<const Prop> prop,
                                const Vector3 &pos, const Quaternion &rot) {
  prop_wrappers_.emplace_back(prop);
  BulletPropWrapper &wrapper = prop_wrappers_.back();

  wrapper.col_shape_ = std::make_shared<btBoxShape>(bulletVector3FromEigen(prop->half_extents_));
  // TODO: generalize to other shapes
  Scalar mass = 8 * prop->half_extents_.prod() * prop->density_;
  btVector3 inertia(0, 0, 0);
  if (mass != 0) {
    wrapper.col_shape_->calculateLocalInertia(mass, inertia);
  }

  btRigidBody::btRigidBodyConstructionInfo rigid_body_info(
      /*mass=*/mass,
      /*motionState=*/nullptr,
      /*collisionShape=*/wrapper.col_shape_.get(),
      /*localInertia=*/inertia);
  rigid_body_info.m_friction = prop->friction_;
  wrapper.rigid_body_ = std::make_shared<btRigidBody>(rigid_body_info);
  wrapper.rigid_body_->setCenterOfMassTransform(btTransform(
      /*q=*/bulletQuaternionFromEigen(rot),
      /*c=*/bulletVector3FromEigen(pos)));
  world_->addRigidBody(wrapper.rigid_body_.get(),
                       /*collisionFilterGroup=*/2,
                       /*collisionFilterMask=*/3);

  return prop_wrappers_.size() - 1;
}

void BulletSimulation::removeRobot(Index robot_idx) {
  auto it = robot_wrappers_.begin() + robot_idx;
  unregisterRobotWrapper(*it);
  robot_wrappers_.erase(it);
}

void BulletSimulation::removeProp(Index prop_idx) {
  auto it = prop_wrappers_.begin() + prop_idx;
  unregisterPropWrapper(*it);
  prop_wrappers_.erase(it);
}

std::shared_ptr<const Robot> BulletSimulation::getRobot(Index robot_idx) const {
  return robot_wrappers_[robot_idx].robot_;
}

std::shared_ptr<const Prop> BulletSimulation::getProp(Index prop_idx) const {
  return prop_wrappers_[prop_idx].prop_;
}

Index BulletSimulation::getRobotCount() const {
  return robot_wrappers_.size();
}

Index BulletSimulation::getPropCount() const {
  return prop_wrappers_.size();
}

Index BulletSimulation::findRobotIndex(const Robot &robot) const {
  for (std::size_t i = 0; i < robot_wrappers_.size(); ++i) {
    if (robot_wrappers_[i].robot_.get() == &robot) { return i; }
  }
  return -1;
}

Index BulletSimulation::findPropIndex(const Prop &prop) const {
  for (std::size_t i = 0; i < prop_wrappers_.size(); ++i) {
    if (prop_wrappers_[i].prop_.get() == &prop) { return i; }
  }
  return -1;
}

void BulletSimulation::unregisterRobotWrapper(BulletRobotWrapper &robot_wrapper) {
  // Remove collision objects for every link
  for (auto collider : robot_wrapper.colliders_) {
    world_->removeCollisionObject(collider.get());
  }
  world_->removeMultiBody(robot_wrapper.multi_body_.get());
}

void BulletSimulation::unregisterPropWrapper(BulletPropWrapper &prop_wrapper) {
  world_->removeRigidBody(prop_wrapper.rigid_body_.get());
}

void BulletSimulation::getLinkTransform(Index robot_idx, Index link_idx,
                                        Matrix4 &transform) const {
  btMultiBody &multi_body = *robot_wrappers_[robot_idx].multi_body_;
  if (link_idx == 0) {
    // Base link
    transform = eigenMatrix4FromBullet(multi_body.getBaseWorldTransform());
  } else {
    transform = eigenMatrix4FromBullet(multi_body.getLink(link_idx - 1).m_cachedWorldTransform);
  }
}

void BulletSimulation::getPropTransform(Index prop_idx, Matrix4 &transform) const {
  btRigidBody &rigid_body = *prop_wrappers_[prop_idx].rigid_body_;
  transform = eigenMatrix4FromBullet(rigid_body.getCenterOfMassTransform());
}

void BulletSimulation::getJointPositions(Index robot_idx, VectorX &pos) const {
  const btMultiBody &multi_body = *robot_wrappers_[robot_idx].multi_body_;
  pos.resize(multi_body.getNumPosVars());
  int offset = 0;
  for (int link_idx = 0; link_idx < multi_body.getNumLinks(); ++link_idx) {
    const btMultibodyLink &link = multi_body.getLink(link_idx);
    for (int pos_var_idx = 0; pos_var_idx < link.m_posVarCount; ++pos_var_idx) {
      pos(offset) = multi_body.getJointPosMultiDof(link_idx)[pos_var_idx];
      ++offset;
    }
  }
}

void BulletSimulation::getJointVelocities(Index robot_idx, VectorX &vel) const {
  const btMultiBody &multi_body = *robot_wrappers_[robot_idx].multi_body_;
  vel.resize(multi_body.getNumDofs());
  int offset = 0;
  for (int link_idx = 0; link_idx < multi_body.getNumLinks(); ++link_idx) {
    const btMultibodyLink &link = multi_body.getLink(link_idx);
    for (int dof_idx = 0; dof_idx < link.m_dofCount; ++dof_idx) {
      vel(offset) = multi_body.getJointVelMultiDof(link_idx)[dof_idx];
      ++offset;
    }
  }
}

void BulletSimulation::addJointTorques(Index robot_idx, VectorX &torque) {
  btMultiBody &multi_body = *robot_wrappers_[robot_idx].multi_body_;
  assert(torque.size() == multi_body.getNumDofs());
  int offset = 0;
  for (int link_idx = 0; link_idx < multi_body.getNumLinks(); ++link_idx) {
    const btMultibodyLink &link = multi_body.getLink(link_idx);
    multi_body.addJointTorqueMultiDof(link_idx, &torque(offset));
    offset += link.m_dofCount;
  }
}

void BulletSimulation::saveState() {
  auto serializer = std::make_shared<btDefaultSerializer>();
  int ser_flags = serializer->getSerializationFlags();
  serializer->setSerializationFlags(ser_flags | BT_SERIALIZE_CONTACT_MANIFOLDS);
  world_->serialize(serializer.get());

  auto bullet_file = std::make_shared<bParse::btBulletFile>(
      (char *)serializer->getBufferPointer(),
      serializer->getCurrentBufferSize());
  bullet_file->parse(false);
  if (bullet_file->ok()) {
    saved_state_ = BulletSavedState(std::move(serializer), std::move(bullet_file));
  } else {
    saved_state_ = BulletSavedState();
  }
}

void BulletSimulation::restoreState() {
  if (saved_state_.bullet_file_ != nullptr) {
    auto importer = std::make_shared<btMultiBodyWorldImporter>(world_.get());
    importer->setImporterFlags(eRESTORE_EXISTING_OBJECTS);
    importer->convertAllObjects(saved_state_.bullet_file_.get());
  }
}

void BulletSimulation::step(Scalar dt) {
  for (auto &robot_wrapper : robot_wrappers_) {
    for (int j = 0; j < robot_wrapper.multi_body_->getNumLinks(); ++j) {
      Scalar pos = robot_wrapper.multi_body_->getJointPos(j);
      Scalar vel = robot_wrapper.multi_body_->getJointVel(j);
      Scalar target_pos = (j % 2 == 0) ? 0.0 : -M_PI / 2;
      Scalar torque = -1.0 * (pos - target_pos) - 0.1 * vel;
      torque = std::max(-1.0, std::min(1.0, torque));
      robot_wrapper.multi_body_->addJointTorque(j, torque);
    }
  }
  world_->stepSimulation(dt, 0, dt);
  world_->forwardKinematics();  // Update m_cachedWorldTransform for every link
}

}  // namespace robot_design
