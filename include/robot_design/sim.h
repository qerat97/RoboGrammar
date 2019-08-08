#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Featherstone/btMultiBodyConstraintSolver.h>
#include <BulletDynamics/Featherstone/btMultiBodyDynamicsWorld.h>
#include <BulletDynamics/Featherstone/btMultiBodyLinkCollider.h>
#include <memory>
#include <robot_design/prop.h>
#include <robot_design/robot.h>
#include <robot_design/types.h>
#include <Serialize/BulletFileLoader/btBulletFile.h>

namespace robot_design {

class Simulation {
public:
  virtual Index addRobot(std::shared_ptr<const Robot> robot, const Vector3 &pos,
                         const Quaternion &rot) = 0;
  virtual Index addProp(std::shared_ptr<const Prop> prop, const Vector3 &pos,
                        const Quaternion &rot) = 0;
  virtual void removeRobot(Index robot_idx) = 0;
  virtual void removeProp(Index prop_idx) = 0;
  virtual std::shared_ptr<const Robot> getRobot(Index robot_idx) const = 0;
  virtual std::shared_ptr<const Prop> getProp(Index prop_idx) const = 0;
  virtual Index getRobotCount() const = 0;
  virtual Index getPropCount() const = 0;
  virtual Index findRobotIndex(const Robot &robot) const = 0;
  virtual Index findPropIndex(const Prop &prop) const = 0;
  virtual void getLinkTransform(Index robot_idx, Index link_idx,
                                Matrix4 &transform) const = 0;
  virtual void getPropTransform(Index prop_idx, Matrix4 &transform) const = 0;
  virtual void getJointPositions(Index robot_idx, VectorX &pos) const = 0;
  virtual void getJointVelocities(Index robot_idx, VectorX &vel) const = 0;
  virtual void addJointTorques(Index robot_idx, VectorX &torque) = 0;
  virtual void saveState() = 0;
  virtual void restoreState() = 0;
  virtual void step(Scalar dt) = 0;
};

struct BulletRobotWrapper {
  BulletRobotWrapper(std::shared_ptr<const Robot> robot)
      : robot_(robot), multi_body_(), col_shapes_(), colliders_() {}
  std::shared_ptr<const Robot> robot_;
  std::shared_ptr<btMultiBody> multi_body_;
  std::vector<std::shared_ptr<btCollisionShape>> col_shapes_;
  std::vector<std::shared_ptr<btMultiBodyLinkCollider>> colliders_;
};

struct BulletPropWrapper {
  BulletPropWrapper(std::shared_ptr<const Prop> prop)
      : prop_(prop), rigid_body_(), col_shape_(), col_object_() {}
  std::shared_ptr<const Prop> prop_;
  std::shared_ptr<btRigidBody> rigid_body_;
  std::shared_ptr<btCollisionShape> col_shape_;
  std::shared_ptr<btCollisionObject> col_object_;
};

struct BulletSavedState {
  BulletSavedState() : serializer_(), bullet_file_() {}
  BulletSavedState(std::shared_ptr<btSerializer> serializer,
                   std::shared_ptr<bParse::btBulletFile> bullet_file)
      : serializer_(serializer), bullet_file_(bullet_file) {}
  std::shared_ptr<btSerializer> serializer_;
  std::shared_ptr<bParse::btBulletFile> bullet_file_;
};

class BulletSimulation : public Simulation {
public:
  BulletSimulation();
  virtual ~BulletSimulation();
  BulletSimulation(const BulletSimulation &other) = delete;
  BulletSimulation &operator=(const BulletSimulation &other) = delete;
  virtual Index addRobot(std::shared_ptr<const Robot> robot, const Vector3 &pos,
                         const Quaternion &rot) override;
  virtual Index addProp(std::shared_ptr<const Prop> prop, const Vector3 &pos,
                        const Quaternion &rot) override;
  virtual void removeRobot(Index robot_idx) override;
  virtual void removeProp(Index prop_idx) override;
  virtual std::shared_ptr<const Robot> getRobot(Index robot_idx) const override;
  virtual std::shared_ptr<const Prop> getProp(Index prop_idx) const override;
  virtual Index getRobotCount() const override;
  virtual Index getPropCount() const override;
  virtual Index findRobotIndex(const Robot &robot) const override;
  virtual Index findPropIndex(const Prop &prop) const override;
  virtual void getLinkTransform(Index robot_idx, Index link_idx,
                                Matrix4 &transform) const override;
  virtual void getPropTransform(Index prop_idx, Matrix4 &transform) const override;
  virtual void getJointPositions(Index robot_idx, VectorX &pos) const override;
  virtual void getJointVelocities(Index robot_idx, VectorX &vel) const override;
  virtual void addJointTorques(Index robot_idx, VectorX &torque) override;
  virtual void saveState() override;
  virtual void restoreState() override;
  virtual void step(Scalar dt) override;

private:
  void unregisterRobotWrapper(BulletRobotWrapper &robot_wrapper);
  void unregisterPropWrapper(BulletPropWrapper &prop_wrapper);

  std::shared_ptr<btDefaultCollisionConfiguration> collision_config_;
  std::shared_ptr<btHashedOverlappingPairCache> pair_cache_;
  std::shared_ptr<btCollisionDispatcher> dispatcher_;
  std::shared_ptr<btDbvtBroadphase> broadphase_;
  std::shared_ptr<btMultiBodyConstraintSolver> solver_;
  std::shared_ptr<btMultiBodyDynamicsWorld> world_;
  std::vector<BulletRobotWrapper> robot_wrappers_;
  std::vector<BulletPropWrapper> prop_wrappers_;
  BulletSavedState saved_state_;
};

}  // namespace robot_design
