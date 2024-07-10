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

  utl::Logger logger_;
  OdbUniquePtr<odb::dbDatabase> db_{nullptr, &odb::dbDatabase::destroy};
  OdbUniquePtr<odb::dbLib> lib_{nullptr, &odb::dbLib::destroy};
  OdbUniquePtr<odb::dbChip> chip_{nullptr, &odb::dbChip::destroy};
  OdbUniquePtr<odb::dbBlock> block_{nullptr, &odb::dbBlock::destroy};
};

// The ConstructPusher tests whether a Pusher object can be constructed
// without halting, and indirectly tests private function Pusher::SetIOBlockages. 
// 
// There are several cases based on the cluster type (see mpl2/object.h):
// 1. HardMacroCluster (ConstructPusherHardMacro)
//     -> Cluster only has leaf_macros_ 
// 2. StdCellCluster   (ConstructPusherStdCell)
//     -> Cluster only has leaf_std_cells_ and dbModules_
// 3. MixedCluster     (ConstructPusherMixed) (in-progress)
//     -> Cluster has both std cells and hard macros

TEST_F(Mpl2PusherTest, ConstructPusherHardMacro)
{
  // Test whether a Cluster of type HardMacroCluster can be created
  // and then used to construct a Pusher object, and then whether the 
  // boundary_to_io_blockage_ created during construction has the expected
  // value (empty).

  utl::Logger* logger_ = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger_);

  odb::dbMaster* master_ = createSimpleMaster(
      db_->findLib("lib"), "simple_master", 1000, 1000, odb::dbMasterType::CORE);

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* inst1 = odb::dbInst::create(block, master_, "leaf_macro");
  
  // Create cluster of type HardMacroCluster, and add one instance
  Cluster* cluster = new Cluster(0, std::string("hard_macro_cluster"), logger_);
  cluster->setClusterType(HardMacroCluster);
  cluster->addLeafMacro(inst1); 

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage_;
  Pusher pusher(logger_, cluster, block, boundary_to_io_blockage_);
  
  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages (-> computeIOSpans, computeIOBlockagesDepth)
  //
  // In this case, the following results would be received:
  // io_spans[L, T, R, B].first = 1.0
  // io_spans[L, T, R, B].second = 0.0
  //
  // Left, top, right, and bottom blockages are computed the second
  // part of each io_span is bigger than the first part, however 
  // in this case this is always untrue (always first > second)
  // so boundary_to_io_blockage_.size() will still be empty at the end.

  EXPECT_TRUE(boundary_to_io_blockage_.size() == 0);
  
}  // ConstructPusherHardMacro

TEST_F(Mpl2PusherTest, ConstructPusherStdCell)
{
  // Test whether a Cluster of type StdCellCluster can be created
  // and then used to construct a Pusher object, and then whether the 
  // boundary_to_io_blockage_ created during construction has the expected
  // values.

  utl::Logger* logger_ = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger_);

  odb::dbMaster* master_ = createSimpleMaster(
      db_->findLib("lib"), "simple_master", 1000, 1000, odb::dbMasterType::CORE);

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst::create(block, master_, "leaf_std_cell1");
  odb::dbInst::create(block, master_, "leaf_std_cell2");
  odb::dbInst::create(block, master_, "leaf_std_cell3");

  Cluster* cluster = new Cluster(0, std::string("stdcell_cluster"), logger_);
  cluster->setClusterType(StdCellCluster);
  cluster->addDbModule(block->getTopModule());
  
  Metrics* metrics = new Metrics(0, 0, 0.0, 0.0);
  for (auto inst : block->getInsts()) {

    const float inst_width = block->dbuToMicrons(
        inst->getBBox()->getBox().dx());
    const float inst_height = block->dbuToMicrons(
        inst->getBBox()->getBox().dy());
    
    cluster->addLeafStdCell(inst);
    metrics->addMetrics(Metrics(1, 0, inst_width * inst_height, 0.0));
  }

  cluster->setMetrics(Metrics(
      metrics->getNumStdCell(),
      metrics->getNumMacro(),
      metrics->getStdCellArea(), 
      metrics->getMacroArea()
  ));

  std::map<Boundary, Rect> boundary_to_io_blockage_;

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  Pusher pusher(logger_, cluster, block, boundary_to_io_blockage_);
  
  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages (-> computeIOSpans, computeIOBlockagesDepth)
  //
  // In this case, the following results would be received:
  // io_spans[L, T, R, B].first = 1.0
  // io_spans[L, T, R, B].second = 0.0
  //
  // Left, top, right, and bottom blockages are computed the second
  // part of each io_span is bigger than the first part, however 
  // in this case this is always untrue (always first > second)
  // so boundary_to_io_blockage_.size() will still be empty at the end.

  EXPECT_TRUE(boundary_to_io_blockage_.empty());

}  // ConstructPusherStdCell

}  // namespace mpl2
