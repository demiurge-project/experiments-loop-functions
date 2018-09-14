#include "GiandujaBeaconAggregationLoopFunc.h"

/****************************************/
/****************************************/

GiandujaBeaconAggregationLoopFunction::GiandujaBeaconAggregationLoopFunction() {
  m_fRadius = 0.3;
  m_cCoordSpot1 = CVector2(0.5,0.5);
  m_cCoordSpot2 = CVector2(-0.5,0.5);
  m_unCostSpot1 = 0;
  m_fObjectiveFunction = 0;
  m_unTime = 0;
  m_unMesParam = 0;
  m_unMes = 0;
}

/****************************************/
/****************************************/

GiandujaBeaconAggregationLoopFunction::GiandujaBeaconAggregationLoopFunction(const GiandujaBeaconAggregationLoopFunction& orig) {}

/****************************************/
/****************************************/

GiandujaBeaconAggregationLoopFunction::~GiandujaBeaconAggregationLoopFunction() {}

/****************************************/
/****************************************/

void GiandujaBeaconAggregationLoopFunction::Destroy() {}

/****************************************/
/****************************************/

void GiandujaBeaconAggregationLoopFunction::Reset() {
    CoreLoopFunctions::Reset();
    m_unCostSpot1 = 0;
    m_fObjectiveFunction = 0;
    if (m_unMesParam == 3) {
        m_unMes = m_pcRng->Uniform(CRange<UInt32>(0,2));
    }
    else {
        m_unMes = m_unMesParam;
    }
    PlaceBeacon();
    //PlaceLight();
}


void GiandujaBeaconAggregationLoopFunction::Init(TConfigurationNode& t_tree) {
    CoreLoopFunctions::Init(t_tree);
    TConfigurationNode cParametersNode;
    try {
        cParametersNode = GetNode(t_tree, "params");
        GetNodeAttributeOrDefault(cParametersNode, "mes", m_unMesParam, (UInt32) 3);
    } catch(std::exception e) {
        LOGERR << e.what() << std::endl;
    }
    if (m_unMesParam == 3) {
        m_unMes = m_pcRng->Uniform(CRange<UInt32>(0,2));
    }
    else {
        m_unMes = m_unMesParam;
    }
    //PlaceLight();
    PlaceBeacon();
    SetMessageBeacon();
}

/****************************************/
/****************************************/

argos::CColor GiandujaBeaconAggregationLoopFunction::GetFloorColor(const argos::CVector2& c_position_on_plane) {
  CVector2 vCurrentPoint(c_position_on_plane.GetX(), c_position_on_plane.GetY());
  Real d = (m_cCoordSpot1 - vCurrentPoint).Length();
  if (d <= m_fRadius) {
    return CColor::WHITE;
  }

  d = (m_cCoordSpot2 - vCurrentPoint).Length();
  if (d <= m_fRadius) {
    return CColor::BLACK;
  }

  return CColor::GRAY50;
}

void GiandujaBeaconAggregationLoopFunction::PlaceBeacon() {
    try {
        CEPuckEntity& cEpuck = dynamic_cast<CEPuckEntity&>(GetSpace().GetEntity("beacon0"));
        MoveEntity(cEpuck.GetEmbodiedEntity(),
                         CVector3(0, 0.5, 0),
                         CQuaternion().FromEulerAngles(CRadians::ZERO,
                         CRadians::ZERO,CRadians::ZERO),false);
     } catch (std::exception& ex) {
         LOGERR << "Error while casting PlaceBeacon: " << ex.what() << std::endl;
     }

}

void GiandujaBeaconAggregationLoopFunction::SetMessageBeacon() {
    try {
        CEPuckEntity& cEntity = dynamic_cast<CEPuckEntity&>(GetSpace().GetEntity("beacon0"));
        CEPuckBeacon& cController = dynamic_cast<CEPuckBeacon&>(cEntity.GetControllableEntity().GetController());
        cController.setMessage(m_unMes);
        if (m_unMes==0) {
            LOG << "Message=" << m_unMes << "blanc" << std::endl;
        }
        else if (m_unMes==1) {
            LOG << "Message=" << m_unMes << "noir"<< std::endl;
        }
    } catch (std::exception& ex) {
        LOGERR << "Error while casting SetMessageBeacon: " << ex.what() << std::endl;
    }
}

CVector3 GiandujaBeaconAggregationLoopFunction::GetRandomPosition() {
    Real a;
    Real b;

    a = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
    b = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));

    Real fPosY = -1.0 + a * 0.8;
    Real fPosX = -0.8 + b * 1.6;

    return CVector3(fPosX, fPosY, 0);
}

/****************************************/
/****************************************/

void GiandujaBeaconAggregationLoopFunction::PostStep() {
    CSpace::TMapPerType& tEpuckMap = GetSpace().GetEntitiesByType("epuck");
    CVector2 cEpuckPosition(0,0);

    for (CSpace::TMapPerType::iterator it = tEpuckMap.begin(); it != tEpuckMap.end(); ++it) {
        CEPuckEntity* pcEpuck = any_cast<CEPuckEntity*>(it->second);
        cEpuckPosition.Set(pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetX(),
                         pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetY());

        Real fDistanceSpot;
        if (m_unMes == 0) {
            fDistanceSpot = (m_cCoordSpot1 - cEpuckPosition).Length();
        }
        else if (m_unMes == 1) {
            fDistanceSpot = (m_cCoordSpot2 - cEpuckPosition).Length();
        }

        if (fDistanceSpot >= m_fRadius) {
            m_unCostSpot1 += 1;
        }
    }
    m_fObjectiveFunction = (Real) m_unCostSpot1;
    m_unTime+=1;
}

/****************************************/
/****************************************/

void GiandujaBeaconAggregationLoopFunction::PostExperiment() {
    LOG<< "fit: " << 25200-m_fObjectiveFunction << std::endl;
}

Real GiandujaBeaconAggregationLoopFunction::GetObjectiveFunction() {
    SInt32 score = 25200-m_fObjectiveFunction;
    return (score);
}

REGISTER_LOOP_FUNCTIONS(GiandujaBeaconAggregationLoopFunction, "gianduja_beacon_aggregation_loop_functions");