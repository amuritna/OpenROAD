#include "../../src/hier_rtlmp.h"
#include "gtest/gtest.h"
#include "helper.h"
#include "mpl2/rtl_mp.h"
#include "odb/db.h"
#include "odb/util.h"
#include "utl/Logger.h"

namespace mpl2 {
class Mpl2PusherTest : public ::testing::Test
{
 protected:
  template <class T>
  using OdbUniquePtr = std::unique_ptr<T, void (*)(T*)>;

  void SetUp() override
  {
    db_ = OdbUniquePtr<odb::dbDatabase>(odb::dbDatabase::create(),
                                        &odb::dbDatabase::destroy);
    chip_ = OdbUniquePtr<odb::dbChip>(odb::dbChip::create(db_.get()),
                                      &odb::dbChip::destroy);
    block
        = OdbUniquePtr<odb::dbBlock>(chip_->getBlock(), &odb::dbBlock::destroy);
  }

  utl::Logger logger;
  OdbUniquePtr<odb::dbDatabase> db_{nullptr, &odb::dbDatabase::destroy};
  OdbUniquePtr<odb::dbLib> lib_{nullptr, &odb::dbLib::destroy};
  OdbUniquePtr<odb::dbChip> chip_{nullptr, &odb::dbChip::destroy};
  OdbUniquePtr<odb::dbBlock> block{nullptr, &odb::dbBlock::destroy};
};

// The ConstructPusher tests whether a Pusher object can be constructed
// without halting, and indirectly tests private function
// Pusher::SetIOBlockages.
//
// There are several cases based on the cluster type (see mpl2/object.h):
// 1. HardMacroCluster (ConstructPusherHardMacro)
//     -> Cluster only has leaf_macros_
// 2. StdCellCluster   (ConstructPusherStdCell)
//     -> Cluster only has leaf_std_cells_ and dbModules_
// 3. MixedCluster     (ConstructPusherMixed)
//     -> Cluster has both std cells and hard macros

TEST_F(Mpl2PusherTest, ConstructPusherHardMacro)
{
  // Test whether a Cluster of type HardMacroCluster can be created
  // and then used to construct a Pusher object.

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(db_->findLib("lib"),
                                              "simple_master",
                                              1000,
                                              1000,
                                              odb::dbMasterType::CORE,
                                              db_->getTech()->findLayer("L1"));

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* inst1 = odb::dbInst::create(block, master_, "leaf_macro");

  // Create cluster of type HardMacroCluster, and add one instance
  Cluster* cluster = new Cluster(0, std::string("hard_macro_cluster"), logger);
  cluster->setClusterType(HardMacroCluster);
  cluster->addLeafMacro(inst1);

  Metrics* metrics = new Metrics(0, 0, 0.0, 0.0);
  metrics->addMetrics(
      Metrics(0,
              1,
              0.0,
              block->dbuToMicrons(inst1->getBBox()->getBox().dx())
                  * block->dbuToMicrons(inst1->getBBox()->getBox().dy())));

  cluster->setMetrics(Metrics(metrics->getNumStdCell(),
                              metrics->getNumMacro(),
                              metrics->getStdCellArea(),
                              metrics->getMacroArea()));

  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage;
  boundary_to_io_blockage[B] = Rect(1, 1, 2, 2);
  boundary_to_io_blockage[L] = Rect(3, 3, 4, 4);
  boundary_to_io_blockage[T] = Rect(1, 7, 2, 8);
  boundary_to_io_blockage[R] = Rect(4, 5, 6, 7);

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  Pusher pusher(logger, cluster, block, boundary_to_io_blockage);

}  // ConstructPusherHardMacro

TEST_F(Mpl2PusherTest, ConstructPusherStdCell)
{
  // Test whether a Cluster of type StdCellCluster can be created
  // and then used to construct a Pusher object.

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(db_->findLib("lib"),
                                              "simple_master",
                                              1000,
                                              1000,
                                              odb::dbMasterType::CORE,
                                              db_->getTech()->findLayer("L1"));

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst::create(block, master_, "leaf_std_cell1");
  odb::dbInst::create(block, master_, "leaf_std_cell2");
  odb::dbInst::create(block, master_, "leaf_std_cell3");

  Cluster* cluster = new Cluster(0, std::string("stdcell_cluster"), logger);
  cluster->setClusterType(StdCellCluster);
  cluster->addDbModule(block->getTopModule());

  Metrics* metrics = new Metrics(0, 0, 0.0, 0.0);
  for (auto inst : block->getInsts()) {
    const float inst_width
        = block->dbuToMicrons(inst->getBBox()->getBox().dx());
    const float inst_height
        = block->dbuToMicrons(inst->getBBox()->getBox().dy());

    cluster->addLeafStdCell(inst);
    metrics->addMetrics(Metrics(1, 0, inst_width * inst_height, 0.0));
  }

  cluster->setMetrics(Metrics(metrics->getNumStdCell(),
                              metrics->getNumMacro(),
                              metrics->getStdCellArea(),
                              metrics->getMacroArea()));

  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage;
  boundary_to_io_blockage[B] = Rect(1, 1, 2, 2);
  boundary_to_io_blockage[L] = Rect(3, 3, 4, 4);
  boundary_to_io_blockage[T] = Rect(1, 7, 2, 8);
  boundary_to_io_blockage[R] = Rect(4, 5, 6, 7);

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  Pusher pusher(logger, cluster, block, boundary_to_io_blockage);

}  // ConstructPusherStdCell

TEST_F(Mpl2PusherTest, ConstructPusherMixed)
{
  // Test whether a Cluster of type MixedCluster can be created
  // and then used to construct a Pusher object.

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(db_->findLib("lib"),
                                              "simple_master",
                                              1000,
                                              1000,
                                              odb::dbMasterType::CORE,
                                              db_->getTech()->findLayer("L1"));

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* i1 = odb::dbInst::create(block, master_, "leaf_std_cell1");
  odb::dbInst* i2 = odb::dbInst::create(block, master_, "leaf_macro1");

  Cluster* cluster = new Cluster(0, std::string("mixed_cluster"), logger);
  cluster->setClusterType(MixedCluster);

  Metrics* metrics = new Metrics(0, 0, 0.0, 0.0);

  cluster->addLeafMacro(i1);
  metrics->addMetrics(
      Metrics(0,
              1,
              0.0,
              block->dbuToMicrons(i1->getBBox()->getBox().dx())
                  * block->dbuToMicrons(i1->getBBox()->getBox().dy())));

  cluster->addLeafStdCell(i2);
  metrics->addMetrics(
      Metrics(1,
              0,
              block->dbuToMicrons(i2->getBBox()->getBox().dx())
                  * block->dbuToMicrons(i2->getBBox()->getBox().dy()),
              0.0));

  cluster->setMetrics(Metrics(metrics->getNumStdCell(),
                              metrics->getNumMacro(),
                              metrics->getStdCellArea(),
                              metrics->getMacroArea()));

  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage;
  boundary_to_io_blockage[B] = Rect(1, 1, 2, 2);
  boundary_to_io_blockage[L] = Rect(3, 3, 4, 4);
  boundary_to_io_blockage[T] = Rect(1, 7, 2, 8);
  boundary_to_io_blockage[R] = Rect(4, 5, 6, 7);

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  Pusher pusher(logger, cluster, block, boundary_to_io_blockage);

}  // ConstructPusherMixed

TEST_F(Mpl2PusherTest, PushHardMacroCluster)
{

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(db_->findLib("lib"),
                                              "simple_master",
                                              1000,
                                              1000,
                                              odb::dbMasterType::CORE,
                                              db_->getTech()->findLayer("L1"));

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* inst1 = odb::dbInst::create(block, master_, "leaf_macro");

  // Create cluster of type HardMacroCluster, and add one instance
  Cluster* cluster = new Cluster(0, std::string("hard_macro_cluster"), logger);
  cluster->setClusterType(HardMacroCluster);
  cluster->addLeafMacro(inst1);

  Metrics* metrics = new Metrics(0, 0, 0.0, 0.0);
  metrics->addMetrics(
      Metrics(0,
              1,
              0.0,
              block->dbuToMicrons(inst1->getBBox()->getBox().dx())
                  * block->dbuToMicrons(inst1->getBBox()->getBox().dy())));

  cluster->setMetrics(Metrics(metrics->getNumStdCell(),
                              metrics->getNumMacro(),
                              metrics->getStdCellArea(),
                              metrics->getMacroArea()));

  
} // PushHardMacroCluster

}  // namespace mpl2
