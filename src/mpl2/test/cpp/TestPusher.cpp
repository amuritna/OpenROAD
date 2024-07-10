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

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(
      db_->findLib("lib"), "simple_master", 1000, 1000, odb::dbMasterType::CORE);

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* inst1 = odb::dbInst::create(block, master_, "leaf_macro");
  
  // Create cluster of type HardMacroCluster, and add one instance
  Cluster* cluster = new Cluster(0, std::string("hard_macro_cluster"), logger);
  cluster->setClusterType(HardMacroCluster);
  cluster->addLeafMacro(inst1); 

  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages (-> computeIOSpans, computeIOBlockagesDepth)
  //
  // In this case, the following results would be received:
  // io_spans[L, T, R, B].first = 1.0
  // io_spans[L, T, R, B].second = 0.0
  std::map<Boundary, std::pair<float, float>> io_spans;
  io_spans[L].first = 1.0;
  io_spans[L].second = 0.0;
  io_spans[T].first = 1.0;
  io_spans[T].second = 0.0;
  io_spans[R].first = 1.0;
  io_spans[R].second = 0.0;
  io_spans[B].first = 1.0;
  io_spans[B].second = 0.0;

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage_;
  Pusher pusher(logger, cluster, block, boundary_to_io_blockage_);
  
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

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  odb::dbMaster* master_ = createSimpleMaster(
      db_->findLib("lib"), "simple_master", 1000, 1000, odb::dbMasterType::CORE);

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

  odb::dbInst* i1 = odb::dbInst::create(block, master_, "leaf_std_cell1");
  odb::dbInst* i2 = odb::dbInst::create(block, master_, "leaf_std_cell2");
  odb::dbInst* i3 = odb::dbInst::create(block, master_, "leaf_std_cell3");

  odb::dbNet* n1 = odb::dbNet::create(block, "n1");
  odb::dbNet* n2 = odb::dbNet::create(block, "n2");
  i1->findITerm("in" )->connect(n1);
  i1->findITerm("out")->connect(n2);

  odb::dbNet* n3 = odb::dbNet::create(block, "n3");
  odb::dbNet* n4 = odb::dbNet::create(block, "n4");
  i2->findITerm("in" )->connect(n3);
  i2->findITerm("out")->connect(n4);

  odb::dbNet* n5 = odb::dbNet::create(block, "n5");
  odb::dbNet* n6 = odb::dbNet::create(block, "n6");
  i3->findITerm("in" )->connect(n5);
  i3->findITerm("out")->connect(n6);
  
  odb::dbBTerm* IN1 = odb::dbBTerm::create(n1, "IN1");
  IN1->setIoType(odb::dbIoType::INPUT);
  odb::dbBTerm* IN2 = odb::dbBTerm::create(n3, "IN2");
  IN2->setIoType(odb::dbIoType::INPUT);
  odb::dbBTerm* IN3 = odb::dbBTerm::create(n5, "IN3");
  IN3->setIoType(odb::dbIoType::INPUT);

  odb::dbBTerm* OUT1 = odb::dbBTerm::create(n2, "OUT1");
  OUT1->setIoType(odb::dbIoType::OUTPUT);
  odb::dbBTerm* OUT2 = odb::dbBTerm::create(n4, "OUT2");
  OUT2->setIoType(odb::dbIoType::OUTPUT);
  odb::dbBTerm* OUT3 = odb::dbBTerm::create(n6, "OUT3");
  OUT3->setIoType(odb::dbIoType::OUTPUT);

  Cluster* cluster = new Cluster(0, std::string("stdcell_cluster"), logger);
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

  // In hier_rtlmp.cpp the io blockages would have been retrieved by
  // setIOClustersBlockages (-> computeIOSpans, computeIOBlockagesDepth)
  //
  // In this case, the following results would be received:
  // io_spans[L, T, R, B].first = 1.0
  // io_spans[L, T, R, B].second = 0.0
  std::map<Boundary, std::pair<float, float>> io_spans;

  /*
  io_spans[L].first = 1.0;
  io_spans[L].second = 0.0;
  io_spans[T].first = 1.0;
  io_spans[T].second = 0.0;
  io_spans[R].first = 1.0;
  io_spans[R].second = 0.0;
  io_spans[B].first = 1.0;
  io_spans[B].second = 0.0;
  */

  odb::Rect die = block->getDieArea();
  io_spans[L] = {
    block->dbuToMicrons(die.yMax()), block->dbuToMicrons(die.yMin())};
  io_spans[T] = {
    block->dbuToMicrons(die.xMax()), block->dbuToMicrons(die.xMin())};
  io_spans[R] = io_spans[L];
  io_spans[B] = io_spans[T];

  for (auto term : block->getBTerms()) {
    int lx = std::numeric_limits<int>::max();
    int ly = std::numeric_limits<int>::max();
    int ux = 0;
    int uy = 0;

    for (const auto pin : term->getBPins()) {
      for (const auto box : pin->getBoxes()) {
        lx = std::min(lx, box->xMin());
        ly = std::min(ly, box->yMin());
        ux = std::max(ux, box->xMax());
        uy = std::max(uy, box->yMax());
      }
    }

    logger->report("lx ly ux uy {} {} {} {}", lx, ly, ux, uy);

    // Modify ranges based on the position of the IO pins.
    if (lx <= die.xMin()) {
      io_spans[L].first = std::min(
          io_spans[L].first, static_cast<float>(block->dbuToMicrons(ly)));
      io_spans[L].second = std::max(
          io_spans[L].second, static_cast<float>(block->dbuToMicrons(uy)));
    } else if (uy >= die.yMax()) {
      io_spans[T].first = std::min(
          io_spans[T].first, static_cast<float>(block->dbuToMicrons(lx)));
      io_spans[T].second = std::max(
          io_spans[T].second, static_cast<float>(block->dbuToMicrons(ux)));
    } else if (ux >= die.xMax()) {
      io_spans[R].first = std::min(
          io_spans[R].first, static_cast<float>(block->dbuToMicrons(ly)));
      io_spans[R].second = std::max(
          io_spans[R].second, static_cast<float>(block->dbuToMicrons(uy)));
    } else {
      io_spans[B].first = std::min(
          io_spans[B].first, static_cast<float>(block->dbuToMicrons(lx)));
      io_spans[B].second = std::max(
          io_spans[B].second, static_cast<float>(block->dbuToMicrons(ux)));
    }
  }

  logger->report(
    "io_spans[L]: {} {},\nio_spans[T]: {} {},\nio_spans[R]: {} {},\nio_spans[B]: {} {},\n",
    io_spans[L].first,
    io_spans[L].second,
    io_spans[T].first,
    io_spans[T].second,
    io_spans[R].first,
    io_spans[R].second,
    io_spans[B].first,
    io_spans[B].second
  );

  // Construct Pusher object, indirectly run Pusher::SetIOBlockages
  std::map<Boundary, Rect> boundary_to_io_blockage_;
  Pusher pusher(logger, cluster, block, boundary_to_io_blockage_);
  
  // Left, top, right, and bottom blockages are computed the second
  // part of each io_span is bigger than the first part, however 
  // in this case this is always untrue (always first > second)
  // so boundary_to_io_blockage_.size() will still be empty at the end.
  EXPECT_TRUE(boundary_to_io_blockage_.empty());

}  // ConstructPusherStdCell

TEST_F(Mpl2PusherTest, ConstructPusherMixed)
{
  // Test whether a Cluster of type StdCellCluster can be created
  // and then used to construct a Pusher object, and then whether the 
  // boundary_to_io_blockage_ created during construction has the expected
  // values.

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = createSimpleDB();
  db_->setLogger(logger);

  createSimpleMaster(
      db_->findLib("lib"), "simple_master", 1000, 1000, odb::dbMasterType::CORE);

  odb::dbBlock* block = odb::dbBlock::create(db_->getChip(), "simple_block");
  block->setDieArea(odb::Rect(0, 0, 1000, 1000));

}  // ConstructPusherMixed

}  // namespace mpl2
