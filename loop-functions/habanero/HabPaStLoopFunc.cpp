/**
  * @file <loop-functions/IcraLoopFunc.cpp>
  *
  * @author Antoine Ligot - <aligot@ulb.ac.be>
  *
  * @license MIT License
  */

#include "HabPaStLoopFunc.h"

/****************************************/
/****************************************/

HabPaStLoopFunction::HabPaStLoopFunction() {
    m_unClock = 0;
    m_unStopTime = 0;
    m_unStopBlock = 0;
    m_fObjectiveFunction = 0;
    m_fPheromoneParameter = 0;
    m_bTrigger = false;
}

/****************************************/
/****************************************/

HabPaStLoopFunction::HabPaStLoopFunction(const HabPaStLoopFunction& orig) {
}

/****************************************/
/****************************************/

HabPaStLoopFunction::~HabPaStLoopFunction() {}

/****************************************/
/****************************************/

void HabPaStLoopFunction::Destroy() {

    m_tRobotStates.clear();
    m_tLEDStates.clear();
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::Init(TConfigurationNode& t_tree) {

    CoreLoopFunctions::Init(t_tree);
    TConfigurationNode cParametersNode;
    try {
      cParametersNode = GetNode(t_tree, "params");
      GetNodeAttributeOrDefault(cParametersNode, "maximization", m_bMaximization, (bool) false);
    } catch(std::exception e) {
    }

    m_cUVColor.SetRed(128);
    m_cUVColor.SetGreen(0);
    m_cUVColor.SetBlue(128);

    InitRobotStates();
    InitPhormicaState();
    InitMocaState();

}

/****************************************/
/****************************************/

void HabPaStLoopFunction::Reset() {
    CoreLoopFunctions::Reset();

    m_pcPhormica->GetLEDEquippedEntity().SetAllLEDsColors(CColor::BLACK);
    m_unClock = 0;
    m_unStopBlock = 0;
    m_unStopTime = 0;
    m_fObjectiveFunction = 0;

    m_tRobotStates.clear();
    m_tLEDStates.clear();

    InitMocaState();
    InitRobotStates();
    InitPhormicaState();
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::PostStep() {

    m_unClock = GetSpace().GetSimulationClock();

    ScoreControl();
    // MocaControl();
    UpdatePhormicaState();
    // LOG << m_fObjectiveFunction << std::endl;
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::PostExperiment() {
    if (m_bMaximization == true){
        LOG << -m_fObjectiveFunction << std::endl;
    }
    else {
        LOG << m_fObjectiveFunction << std::endl;
    }
}

/****************************************/
/****************************************/

Real HabPaStLoopFunction::GetObjectiveFunction() {
    if (m_bMaximization == true){
        return -m_fObjectiveFunction;
    }
    else {
        return m_fObjectiveFunction;
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::MocaControl() {

    if (m_unClock == m_unStopTime) {
        CSpace::TMapPerType& tBlocksMap = GetSpace().GetEntitiesByType("block");
        UInt32 unBlocksID = 0;
        for (CSpace::TMapPerType::iterator it = tBlocksMap.begin(); it != tBlocksMap.end(); ++it) {
            CBlockEntity* pcBlock = any_cast<CBlockEntity*>(it->second);
            if (unBlocksID == m_unStopBlock) {
                pcBlock->GetLEDEquippedEntity().SetAllLEDsColors(CColor::GREEN);
                break;
            }
            unBlocksID += 1;
        }
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::ScoreControl(){

    if (m_bTrigger)
        m_fObjectiveFunction += GetStopScore();
    else
        m_fObjectiveFunction += GetMoveScore();
}

/****************************************/
/****************************************/

Real HabPaStLoopFunction::GetStopScore() {

    UpdateRobotPositions();

    Real unScore = 0;
    TRobotStateMap::iterator it;
    for (it = m_tRobotStates.begin(); it != m_tRobotStates.end(); ++it) {
        Real d = (it->second.cPosition - it->second.cLastPosition).Length();
        if (d > 0.0005)
            unScore+=1;
    }

    return unScore;
}

/****************************************/
/****************************************/

Real HabPaStLoopFunction::GetMoveScore() {

    UpdateRobotPositions();

    Real unScore = 0;
    TRobotStateMap::iterator it;
    for (it = m_tRobotStates.begin(); it != m_tRobotStates.end(); ++it) {
        Real d = (it->second.cPosition - it->second.cLastPosition).Length();
        if (d <= 0.0005)
            unScore+=1;
    }

    return unScore;
}

/****************************************/
/****************************************/

argos::CColor HabPaStLoopFunction::GetFloorColor(const argos::CVector2& c_position_on_plane) {

  if (c_position_on_plane.GetX() < -0.255 && c_position_on_plane.GetX() > -0.375 && c_position_on_plane.GetY() < -0.525 && c_position_on_plane.GetY() > -0.645)
      return CColor::BLACK;


    return CColor::WHITE;
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::UpdateRobotPositions() {
    CSpace::TMapPerType& tEpuckMap = GetSpace().GetEntitiesByType("epuck");
    CVector2 cEpuckPosition(0,0);
    for (CSpace::TMapPerType::iterator it = tEpuckMap.begin(); it != tEpuckMap.end(); ++it) {
        CEPuckEntity* pcEpuck = any_cast<CEPuckEntity*>(it->second);
        cEpuckPosition.Set(pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetX(),
                           pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetY());

        m_tRobotStates[pcEpuck].cLastPosition = m_tRobotStates[pcEpuck].cPosition;
        m_tRobotStates[pcEpuck].cPosition = cEpuckPosition;

        // If a robot steps on the patch
        if (cEpuckPosition.GetX() < -0.255 && cEpuckPosition.GetX() > -0.375 && cEpuckPosition.GetY() < -0.525 && cEpuckPosition.GetY() > -0.645) {
          // LOG << "On the floor"<< std::endl;
          if (!m_bTrigger)
          m_bTrigger = true;
        }


        // Updating the Pheromone trail width parameter w.r.t ON UV LEDs

        CColor cEpuckUVOne = pcEpuck->GetLEDEquippedEntity().GetLED(11).GetColor();
        CColor cEpuckUVTwo = pcEpuck->GetLEDEquippedEntity().GetLED(12).GetColor();
        CColor cEpuckUVThree = pcEpuck->GetLEDEquippedEntity().GetLED(13).GetColor();

        if (cEpuckUVOne == CColor::BLACK && cEpuckUVTwo == m_cUVColor && cEpuckUVThree == CColor::BLACK){
            //m_fPheromoneParameter = 0.02; // Thin pheromone trail
            m_tRobotStates[pcEpuck].unPheromoneLEDs = 3;
        }
        else if (cEpuckUVOne == m_cUVColor && cEpuckUVTwo == m_cUVColor && cEpuckUVThree == m_cUVColor){
            //m_fPheromoneParameter = 0.045; // Thick pheromone trails
            m_tRobotStates[pcEpuck].unPheromoneLEDs = 9;
        }
        else {
            //m_fPheromoneParameter = 0; // No pheromone trail
            m_tRobotStates[pcEpuck].unPheromoneLEDs = 0;
        }
        //LOG<< m_fPheromoneParameter<< "Robot update LED color" << std::endl;
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::InitRobotStates() {

    CSpace::TMapPerType& tEpuckMap = GetSpace().GetEntitiesByType("epuck");
    CVector2 cEpuckPosition(0,0);
    for (CSpace::TMapPerType::iterator it = tEpuckMap.begin(); it != tEpuckMap.end(); ++it) {
        CEPuckEntity* pcEpuck = any_cast<CEPuckEntity*>(it->second);
        cEpuckPosition.Set(pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetX(),
                           pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetY());

        m_tRobotStates[pcEpuck].cLastPosition = cEpuckPosition;
        m_tRobotStates[pcEpuck].cPosition = cEpuckPosition;
        m_tRobotStates[pcEpuck].unPheromoneLEDs = 0;
        m_tRobotStates[pcEpuck].unItem = 0;
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::InitPhormicaState() {

    CSpace::TMapPerType& tPhormicaMap = GetSpace().GetEntitiesByType("phormica");
    CVector2 cLEDPosition(0,0);
    for (CSpace::TMapPerType::iterator it = tPhormicaMap.begin(); it != tPhormicaMap.end(); ++it) {
        CPhormicaEntity* pcPhormica = any_cast<CPhormicaEntity*>(it->second);
        m_pcPhormica = pcPhormica;
        m_pcPhormica->GetLEDEquippedEntity().Enable();
        m_pcPhormica->GetLEDEquippedEntity().SetAllLEDsColors(CColor::BLACK);
        m_unNumberLEDs = m_pcPhormica->GetLEDEquippedEntity().GetLEDs().size();
        for (UInt32 i = 0; i < m_unNumberLEDs; ++i) {
            cLEDPosition.Set(m_pcPhormica->GetLEDEquippedEntity().GetLED(i).GetPosition().GetX(),
                             m_pcPhormica->GetLEDEquippedEntity().GetLED(i).GetPosition().GetY());
            m_tLEDStates[i].unLEDIndex = i;
            m_tLEDStates[i].cLEDPosition = cLEDPosition;
            m_tLEDStates[i].unTimer = 0;
        }
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::UpdatePhormicaState() {

    TLEDStateMap::iterator itLED;
    TRobotStateMap::iterator it;
    for (itLED = m_tLEDStates.begin(); itLED != m_tLEDStates.end(); ++itLED) {

        for (it = m_tRobotStates.begin(); it != m_tRobotStates.end(); ++it) {
            Real d = (itLED->second.cLEDPosition - it->second.cPosition).Length();
            Real fPheromone = 0;

            if (it->second.unPheromoneLEDs <= 0)
                fPheromone = 0;
            else if (it->second.unPheromoneLEDs <= 3)
                fPheromone = 0.02;
            else
                fPheromone = 0.045;

            //if (d <= m_fPheromoneParameter) {
            if (d <= fPheromone) {
                itLED->second.unTimer = 400; // Pheromone decay time
                m_pcPhormica->GetLEDEquippedEntity().SetLEDColor(itLED->second.unLEDIndex,CColor::MAGENTA);
                //LOG << "Colored LED: " << itLED->second.unLEDIndex << std::endl;
            }
        }

        UInt32 unLEDTimer = itLED->second.unTimer;
        if (unLEDTimer == 0){
            m_pcPhormica->GetLEDEquippedEntity().SetLEDColor(itLED->second.unLEDIndex,CColor::BLACK);
        }
        else {
            itLED->second.unTimer = unLEDTimer - 1;
        }
    }
}

/****************************************/
/****************************************/

void HabPaStLoopFunction::InitMocaState() {

    CSpace::TMapPerType& tBlocksMap = GetSpace().GetEntitiesByType("block");
    for (CSpace::TMapPerType::iterator it = tBlocksMap.begin(); it != tBlocksMap.end(); ++it) {
        CBlockEntity* pcBlock = any_cast<CBlockEntity*>(it->second);
        pcBlock->GetLEDEquippedEntity().Enable();
        pcBlock->GetLEDEquippedEntity().SetAllLEDsColors(CColor::BLACK);
    }
}

/****************************************/
/****************************************/

CVector3 HabPaStLoopFunction::GetRandomPosition() {
  // Real temp;
  // Real a = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
  // Real b = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
  // Real c = m_pcRng->Uniform(CRange<Real>(-1.0f, 1.0f));
  // Real d = m_pcRng->Uniform(CRange<Real>(-1.0f, 1.0f));
  // // If b < a, swap them
  // if (b < a) {
  //   temp = a;
  //   a = b;
  //   b = temp;
  // }
  // m_fDistributionRadius = 0.35;
  // Real fPosX = (c * m_fDistributionRadius / 2) + m_fDistributionRadius * cos(2 * -CRadians::PI_OVER_TWO .GetValue() * (a/b));
  // Real fPosY = (d * 1.4 * m_fDistributionRadius / 2) + 1.4 * m_fDistributionRadius * sin(2 * -CRadians::PI_OVER_TWO.GetValue() * (a/b));
  //
  // return CVector3(fPosX, fPosY, 0);

  Real a;
  Real b;

  a = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
  b = m_pcRng->Uniform(CRange<Real>(-1.0f, 1.0f));

  Real fPosY = 0.5 + a * 0.3;
  Real fPosX = 0.0 + b * 0.35;

  return CVector3(fPosX, fPosY, 0);
}

/****************************************/
/****************************************/

UInt32 HabPaStLoopFunction::GetRandomTime(UInt32 unMin, UInt32 unMax) {
  UInt32 unStopAt = m_pcRng->Uniform(CRange<UInt32>(unMin, unMax));
  return unStopAt;

}

/****************************************/
/****************************************/

bool HabPaStLoopFunction::IsEven(UInt32 unNumber) {
    bool even;
    if((unNumber%2)==0)
       even = true;
    else
       even = false;

    return even;
}

/****************************************/
/****************************************/

REGISTER_LOOP_FUNCTIONS(HabPaStLoopFunction, "hab_pa_st_loop_function");
