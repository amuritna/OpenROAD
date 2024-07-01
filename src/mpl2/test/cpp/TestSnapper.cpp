#include "gtest/gtest.h"
#include "mpl2/rtl_mp.h"
#include "../../src/hier_rtlmp.h"
#include "odb/db.h"
#include "odb/util.h"
#include "utl/Logger.h"

namespace mpl2 {
class Mpl2SnapperTest : public ::testing::Test
{
 protected:
  template <class T>
  using OdbUniquePtr = std::unique_ptr<T, void (*)(T*)>;

  void SetUp() override
  {
    utl::Logger* logger = new utl::Logger();

    db_ = OdbUniquePtr<odb::dbDatabase>(odb::dbDatabase::create(),
                                        &odb::dbDatabase::destroy);
    chip_ = OdbUniquePtr<odb::dbChip>(odb::dbChip::create(db_.get()),
                                      &odb::dbChip::destroy);
    block_ = OdbUniquePtr<odb::dbBlock>(
        chip_->getBlock(), &odb::dbBlock::destroy);
    
  }

  utl::Logger logger_;
  OdbUniquePtr<odb::dbDatabase> db_{nullptr, &odb::dbDatabase::destroy};
  OdbUniquePtr<odb::dbLib> lib_{nullptr, &odb::dbLib::destroy};
  OdbUniquePtr<odb::dbChip> chip_{nullptr, &odb::dbChip::destroy};
  OdbUniquePtr<odb::dbBlock> block_{nullptr, &odb::dbBlock::destroy};
};

TEST_F(Mpl2SnapperTest, CanSetMacroForEmptyInstances)
{
  // create a simple block and then add 3 instances to that block
  // without any further configuration to each instance,
  // and then run setMacro(inst) on each instance

  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = odb::dbDatabase::create();
  db_->setLogger(logger);
  
  odb::dbTech* tech_ = odb::dbTech::create(db_, "tech");
  odb::dbLib* lib_ = odb::dbLib::create(db_, "lib", tech_, ',');
  odb::dbTechLayer::create(tech_, "L1", odb::dbTechLayerType::MASTERSLICE);
  odb::dbChip* chip_ = odb::dbChip::create(db_);

  odb::dbMaster* master_ = odb::dbMaster::create(lib_, "simple_master");
  master_->setWidth(1000);
  master_->setHeight(1000);
  master_->setType(odb::dbMasterType::CORE);
  odb::dbMTerm::create(master_, "in", odb::dbIoType::INPUT, odb::dbSigType::SIGNAL);
  odb::dbMTerm::create(master_, "out", odb::dbIoType::OUTPUT, odb::dbSigType::SIGNAL);
  master_->setFrozen();
  
  odb::dbBlock* block_ = odb::dbBlock::create(chip_, "simple_block");
  block_->setDieArea(odb::Rect(0, 0, 1000, 1000));
  
  odb::dbDatabase::beginEco(block_);
  odb::dbInst* inst1 = odb::dbInst::create(block_, master_, "cells_1");
  odb::dbInst* inst2 = odb::dbInst::create(block_, master_, "cells_2");
  odb::dbInst* inst3 = odb::dbInst::create(block_, master_, "cells_3");
  odb::dbDatabase::endEco(block_);

  Snapper snapper;
  snapper.setMacro(inst1);
  snapper.setMacro(inst2);
  snapper.setMacro(inst3);
}

TEST_F(Mpl2SnapperTest, CanSnapMacros)
{
  // when snapMacro is called, it later calls
  // computeSnapOrigin (which calls computeSnapParameters,
  // getOrigin, getPitch, getOffset, etc) then setOrigin
  //
  // computeSnapOrigin:
  // - gets instance master, then each MTerm->MPins in master
  // - and then getGeometry in MPin -> add layers to snap_layers
  // - direction of each layer is checked and used as input for
  //   computeSnapParameters
  // computeSnapParameters:
  // - receives input dbTechLayer*, dbBox*, layer direction
  // - gets block of instance, then track grid, then pitch,
  //   offset, pin width, lower-left to first pin
  // - returns computation results
  // inst->setOrigin
  // - can be checked using inst->getOrigin
  
  utl::Logger* logger = new utl::Logger();
  odb::dbDatabase* db_ = odb::dbDatabase::create();
  db_->setLogger(logger);
  
  odb::dbTech* tech_ = odb::dbTech::create(db_, "tech");
  odb::dbLib* lib_ = odb::dbLib::create(db_, "lib", tech_, ',');
  odb::dbChip* chip_ = odb::dbChip::create(db_);
  odb::dbTechLayer* layer1_ = odb::dbTechLayer::create(tech_, "layer1", odb::dbTechLayerType::CUT);
  odb::dbTechLayer* layer2_ = odb::dbTechLayer::create(tech_, "layer2", odb::dbTechLayerType::CUT);

  // During the construction of a new HardMacro object
  // with input dbInst* inst, the following values are retrieved:
  // inst->getBlock, inst->getName, inst->getMaster,
  // master->getWidth, master->getHeight, master->getMTerms,
  // mterm->getSigType, mterm->getMPins,
  // mpin->getGeometry, box->getBox (see: object.cpp)

  odb::dbMaster* master_ = odb::dbMaster::create(lib_, "simple_master");
  master_->setWidth(1000);
  master_->setHeight(1000);
  master_->setType(odb::dbMasterType::BLOCK);
  odb::dbMTerm* mterm_i = odb::dbMTerm::create(master_, "in", odb::dbIoType::INPUT, odb::dbSigType::SIGNAL);
  odb::dbMTerm* mterm_o = odb::dbMTerm::create(master_, "out", odb::dbIoType::OUTPUT, odb::dbSigType::SIGNAL);
  odb::dbMPin* mpin_i = odb::dbMPin::create(mterm_i);
  odb::dbMPin* mpin_o = odb::dbMPin::create(mterm_o);
  master_->setFrozen();

  logger->report(
    "bbox of mpin_i: ({}, {}, {}, {})",
    mpin_i->getBBox().xMin(),
    mpin_i->getBBox().yMin(),
    mpin_i->getBBox().xMax(),
    mpin_i->getBBox().yMax()
  );

  logger->report(
    "bbox of mpin_o: ({}, {}, {}, {})",
    mpin_o->getBBox().xMin(),
    mpin_o->getBBox().yMin(),
    mpin_o->getBBox().xMax(),
    mpin_o->getBBox().yMax()
  );

  for (odb::dbBox* box : mpin_i->getGeometry()) {
    logger->report("box found");
    logger->report("tech layer: {}", box->getTechLayer()->getName());
  }
  
  odb::dbBlock* block_ = odb::dbBlock::create(chip_, "simple_block");
  block_->setDieArea(odb::Rect(0, 0, 1000, 1000));
  
  odb::dbDatabase::beginEco(block_);
  odb::dbInst* inst1 = odb::dbInst::create(block_, master_, "cells_1");
  odb::dbDatabase::endEco(block_);


  Snapper snapper;
  snapper.setMacro(inst1);
  
  inst1->setOrigin(0, 0);
  // program stops when running getOrigin()
  // setOrigin doesn't seem to have an effect?

  logger->report(
    "initial (x, y) of inst1: ({}, {})",
    inst1->getOrigin().x(),
    inst1->getOrigin().y()
  );

  // - gets instance master, then each MTerm->MPins in master
  // it is expected that dbMaster is of isBlock type
  odb::dbMaster* master = inst1->getMaster();
  logger->report(
    "master of inst1: {}, isBlock status: {}, isCore status: {}, isPad status: {}",
    master->getName(),
    master->isBlock(),
    master->isCore(),
    master->isPad()
  );

  for (odb::dbMTerm* mterm : master->getMTerms()) {
    logger->report(
      "mterm: {}, is type signal: {}",
      mterm->getName(),
      mterm->getSigType() == odb::dbSigType::SIGNAL
    );

    for (odb::dbMPin* mpin : mterm->getMPins()) {
      logger->report(
        "found mpin belonging to {}",
        mpin->getMTerm()->getName()
      );


      
    }
  }

  snapper.snapMacro(); // expected to change inst1->getOrigin

  logger->report(
    "end (x, y) of inst1: ({}, {})",
    inst1->getOrigin().x(),
    inst1->getOrigin().y()
  );
}

}  // namespace mpl2