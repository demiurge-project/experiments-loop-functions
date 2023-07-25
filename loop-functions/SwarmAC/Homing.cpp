/**
  * @file <loop-functions/AggregationTwoSpotsLoopFunc.cpp>
  *
  * @author Antoine Ligot - <aligot@ulb.ac.be>
  *
  * @license MIT License
  */

#include "Homing.h"

/****************************************/
/****************************************/

Homing::Homing() {
  m_fRadius = 0.3;
  m_cCoordSpot1 = CVector2(0,-0.7);
  m_unScoreSpot1 = 0;
  m_fObjectiveFunction = 0;
  m_fHeightNest = 0.6;
}

/****************************************/
/****************************************/

Homing::Homing(const Homing& orig) {}

/****************************************/
/****************************************/

void Homing::Init(TConfigurationNode& t_tree) {
    CoreLoopFunctions::Init(t_tree);
    TConfigurationNode cParametersNode;
    try {
      cParametersNode = GetNode(t_tree, "params");
    } catch(std::exception e) {
    }

    m_context = zmq::context_t(1);
    m_socket_actor = zmq::socket_t(m_context, ZMQ_PUSH);
    m_socket_actor.connect("tcp://localhost:5555");

    m_socket_critic = zmq::socket_t(m_context, ZMQ_PUSH);
    m_socket_critic.connect("tcp://localhost:5556");

    // Critic network

    // Initialise traces and delta
    delta = 0;
    policy_trace.resize(100);   // For Dandelion
    std::fill(policy_trace.begin(), policy_trace.end(), 0.0f);
    value_trace.resize(2501);
    std::fill(value_trace.begin(), value_trace.end(), 0.0f);
}

/****************************************/
/****************************************/


Homing::~Homing() {}

/****************************************/
/****************************************/

void Homing::Destroy() {}

/****************************************/
/****************************************/

argos::CColor Homing::GetFloorColor(const argos::CVector2& c_position_on_plane) {
  CVector2 vCurrentPoint(c_position_on_plane.GetX(), c_position_on_plane.GetY());
  Real d = (m_cCoordSpot1 - vCurrentPoint).Length();
  if (d <= m_fRadius) {
    return CColor::BLACK;
  }

  return CColor::GRAY50;
}


/****************************************/
/****************************************/

void Homing::Reset() {
  m_fObjectiveFunction = 0;
  m_unScoreSpot1 = 0;

  delta = 0;
  policy_trace.resize(100);
  std::fill(policy_trace.begin(), policy_trace.end(), 0.0f);
  value_trace.resize(2501);
  std::fill(value_trace.begin(), value_trace.end(), 0.0f);

  CoreLoopFunctions::Reset();
}

/****************************************/
/****************************************/

void Homing::PreStep() {
  std::cout << "Prestep\n";
  // Observe state S
  /* Check position for each agent. 
   * If robot is in cell i --> 1
   * else 0
   */
  at::Tensor grid = torch::zeros({50, 50});
  CSpace::TMapPerType& tEpuckMap = GetSpace().GetEntitiesByType("epuck");
  CVector2 cEpuckPosition(0,0);
  for (CSpace::TMapPerType::iterator it = tEpuckMap.begin(); it != tEpuckMap.end(); ++it) {
    CEPuckEntity* pcEpuck = any_cast<CEPuckEntity*>(it->second);
    cEpuckPosition.Set(pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetX(),
                       pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetY());
    
    // Scale the position to the grid size
    int grid_x = static_cast<int>(std::round((cEpuckPosition.GetX() + 1.231) / 2.462 * 49));
    int grid_y = static_cast<int>(std::round((-cEpuckPosition.GetY() + 1.231) / 2.462 * 49));

    // Set the grid cell that corresponds to the epuck's position to 1
    grid[grid_x][grid_y] = 1;
  }
  // Flatten the 2D tensor to 1D for net input
  state = grid.view({-1}).clone();
  state.set_requires_grad(true);

  // Load up-to-date value network
  std::vector<float> critic;
  std::vector<float> critic_weights;
  std::vector<float> critic_bias;

  // Assuming dimensions of weights {output_features, input_features} for fc1 layer
  m_value->mutex.lock();
  for(auto w : m_value->vec){
      critic.push_back(w);
      if(critic_weights.size() < 2500){
        critic_weights.push_back(w);
      }else if (critic_weights.size() == 2500)
      {
        critic_bias.push_back(w);
      }
    //std::cout << w << ", ";
  }
  m_value->mutex.unlock();
  //std::cout << "Critic parameters: " << critic_net.parameters() << std::endl;
  // Upadate critic weights and bias
  for (auto& p : critic_net.named_parameters()) {
      if (p.key() == "fc.weight") {
        //std::cout << "critic weight accumulate: " << accumulate(critic_weights.begin(),critic_weights.end(),0.0) << std::endl;
        //std::cout << "Critic: " << critic << std::endl;
        torch::Tensor new_weight_tensor = torch::from_blob(critic_weights.data(), {1, 2500});
        p.value().data().copy_(new_weight_tensor);
      } else if (p.key() == "fc.bias") {
        //std::cout << p.key() << "_critic: " << p.value() << std::endl;
        torch::Tensor new_bias_tensor = torch::from_blob(critic_bias.data(), {1});
        p.value().data().copy_(new_bias_tensor);
      }
  }

  // Load up-to-date policy network to each robot
  std::vector<float> actor;
  m_policy->mutex.lock();
  // 96 weights (24 * 4) + 4 bias for Dandelion
  // 144 weights (24 * 6) + 6 bias for Daisy
  for(auto w : m_policy->vec){
    actor.push_back(w);
    //std::cout << w << ", ";
  }
  m_policy->mutex.unlock();
  //std::cout <<" actor\n";
  
  // Launch the experiment with the correct random seed and network,
  // and evaluate the average fitness
  CSpace::TMapPerType cEntities = GetSpace().GetEntitiesByType("controller");
  for (CSpace::TMapPerType::iterator it = cEntities.begin();
       it != cEntities.end(); ++it) {
    CControllableEntity *pcEntity =
        any_cast<CControllableEntity *>(it->second);
    try {
      CEpuckNNController& cController =
      dynamic_cast<CEpuckNNController&>(pcEntity->GetController());
      cController.LoadNetwork(actor);
    } catch (std::exception &ex) {
      LOGERR << "Error while setting network: " << ex.what() << std::endl;
    }
  }
}

/****************************************/
/****************************************/

void Homing::PostStep() {
  std::cout << "PostStep\n";
  at::Tensor grid = torch::zeros({50, 50});
  CSpace::TMapPerType& tEpuckMap = GetSpace().GetEntitiesByType("epuck");
  CVector2 cEpuckPosition(0,0);
  m_unScoreSpot1 = 0;	
  for (CSpace::TMapPerType::iterator it = tEpuckMap.begin(); it != tEpuckMap.end(); ++it) {
    CEPuckEntity* pcEpuck = any_cast<CEPuckEntity*>(it->second);
    cEpuckPosition.Set(pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetX(),
                       pcEpuck->GetEmbodiedEntity().GetOriginAnchor().Position.GetY());

    
    // Scale the position to the grid size
    int grid_x = static_cast<int>(std::round((cEpuckPosition.GetX() + 1.231) / 2.462 * 49));
    int grid_y = static_cast<int>(std::round((-cEpuckPosition.GetY() + 1.231) / 2.462 * 49));
    
    Real fDistanceSpot1 = (m_cCoordSpot1 - cEpuckPosition).Length();
    if (fDistanceSpot1 <= m_fRadius) {
      m_unScoreSpot1 += 1;    // Reward
      m_fObjectiveFunction += 1;
      //std::cout << "Robot at [" << grid_x << ";" << grid_y << "]\n";      
    }
    
    // Set the grid cell that corresponds to the epuck's position to 1
    //grid(grid_y, grid_x) = 1;
    grid[grid_x][grid_y] = 1;
  }
  
  //if(m_unScoreSpot1 > 0){
    std::cout << "score: " << m_unScoreSpot1 << std::endl;	  
    // place spot in grid
    int grid_x = static_cast<int>(std::round((m_cCoordSpot1.GetX() + 1.231) / 2.462 * 49));
    int grid_y = static_cast<int>(std::round((-m_cCoordSpot1.GetY() + 1.231) / 2.462 * 49));
    int radius = static_cast<int>(std::round(0.35 / 2.462 * 49));
    // Print arena on the terminal
    auto temp1 = grid[grid_x][grid_y];
    auto temp2 = grid[grid_x+radius][grid_y];
    auto temp3 = grid[grid_x-radius][grid_y];
    auto temp4 = grid[grid_x][grid_y+radius];
    auto temp5 = grid[grid_x][grid_y-radius];
    grid[grid_x][grid_y] = 2;
    grid[grid_x+radius][grid_y] = 3;
    grid[grid_x-radius][grid_y] = 3;
    grid[grid_x][grid_y+radius] = 3;
    grid[grid_x][grid_y-radius] = 3;
    std::cout << "State:\n";
    //print_grid(grid);
    grid[grid_x][grid_y] = temp1;
    grid[grid_x+radius][grid_y] = temp2;
    grid[grid_x-radius][grid_y] = temp3;
    grid[grid_x][grid_y+radius] = temp4;
    grid[grid_x][grid_y-radius] = temp5;
  //}
  //m_fObjectiveFunction = m_unScoreSpot1/(Real) m_unNumberRobots;
  //LOG << "Score = " << m_fObjectiveFunction << std::endl;

  // Observe state S'
  /* Check position for each agent. 
   * If robot is in grid --> 1
   * else 0
   */
  // Flatten the 2D tensor to 1D for net input
  state_prime = grid.view({-1}).clone();

  // Compute delta with Critic predictions
  //Matrix v_state = m_criticNet.predict(state);
  torch::Tensor v_state = critic_net.forward(state);
  std::cout << "v(s) = " << v_state[0].item<float>() << std::endl;
  //Matrix v_state_prime = m_criticNet.predict(state_prime);
  torch::Tensor v_state_prime = critic_net.forward(state_prime);
  std::cout << "v(s') = " << v_state_prime[0].item<float>() << std::endl;
  //delta = m_fObjectiveFunction + v_state(0) - v_state_prime(0);
  delta = m_unScoreSpot1 + v_state[0].item<float>() - v_state_prime[0].item<float>();
  //std::cout << "delta = " << delta << std::endl;

  // Compute value trace
  // Zero out the gradients
  critic_net.zero_grad();
  // Compute the gradient by back-propagation
  v_state.backward();
  auto params = critic_net.named_parameters();
  auto weight_grad = params["fc.weight"].grad();
  auto bias_grad = params["fc.bias"].grad();
  //std::cout << "Weight gradients: " << weight_grad << std::endl;
  //std::cout << "Bias gradients: " << bias_grad << std::endl;
  weight_grad = weight_grad.view({-1});
  bias_grad = bias_grad.view({-1});
  float *weight_data = weight_grad.data_ptr<float>();
  float lambda_critic = 0.8;
  for (int i = 0; i < 2500; ++i) {
	  value_trace[i] = lambda_critic * value_trace[i] + weight_data[i];
  }
  float *bias_data = bias_grad.data_ptr<float>();
  value_trace[2500] = lambda_critic * value_trace[2500] + bias_data[0];
  //std::cout << "value trace: " << value_trace << std::endl;
  //std::cout << "accumulate of trace: " << accumulate(value_trace.begin(),value_trace.end(),0.0) << std::endl;

  // Compute policy trace
  // TODO: add gradiant
  /*
  for(auto& element : policy_trace) {
    element *= 0.01;     //lambda
    //std::cout << element << ", ";
  }
  */
  //std::cout <<" policy_tarce\n";
  
  CSpace::TMapPerType cEntities = GetSpace().GetEntitiesByType("controller");  
  for (CSpace::TMapPerType::iterator it = cEntities.begin();
		  it != cEntities.end(); ++it) {
	  CControllableEntity *pcEntity =
		  any_cast<CControllableEntity *>(it->second);	    
	  try {
		  CEpuckDandelController& cController =
			  dynamic_cast<CEpuckDandelController&>(pcEntity->GetController());
		  std::vector<float> trace = cController.get_policy_trace();
		  std::cout << "robot policy trace: " << trace << std::endl;
		  for (int i = 0; i < 100; ++i) {			            
			  policy_trace[i] += trace[i];			
    		  }
	  } catch (std::exception &ex) {
		  LOGERR << "Error while updating policy trace: " << ex.what() << std::endl;
	  }
  }
  std::cout << "swarm policy trace: " << policy_trace << std::endl;
  // actor
  Data policy_data {delta, policy_trace};   
  std::string serialized_data = serialize(policy_data);
  zmq::message_t message(serialized_data.size());
  memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
  m_socket_actor.send(message, zmq::send_flags::dontwait);
  // critic
  Data value_data {delta, value_trace};   
  serialized_data = serialize(value_data);
  message.rebuild(serialized_data.size());
  memcpy(message.data(), serialized_data.c_str(), serialized_data.size());
  m_socket_critic.send(message, zmq::send_flags::dontwait);  

}

/****************************************/
/****************************************/

void Homing::PostExperiment() {
  LOG << "Score = " << m_fObjectiveFunction << std::endl;
}

/****************************************/
/****************************************/

Real Homing::GetObjectiveFunction() {
  return m_fObjectiveFunction;
}

/****************************************/
/****************************************/

CVector3 Homing::GetRandomPosition() {
  Real temp, a, b, fPosX, fPosY;
  bool bPlaced = false;
  do {
      a = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
      b = m_pcRng->Uniform(CRange<Real>(0.0f, 1.0f));
      // If b < a, swap them
      if (b < a) {
        temp = a;
        a = b;
        b = temp;
      }
      fPosX = b * m_fDistributionRadius * cos(2 * CRadians::PI.GetValue() * (a/b));
      fPosY = b * m_fDistributionRadius * sin(2 * CRadians::PI.GetValue() * (a/b));

      if (fPosY >= m_fHeightNest) {
        bPlaced = true;
      }
  } while(!bPlaced);
  return CVector3(fPosX, fPosY, 0);
}

/****************************************/
/****************************************/

// Function to compute the log-PDF of the Beta distribution
double computeBetaLogPDF(double alpha, double beta, double x) {
    // Compute the logarithm of the Beta function
    double logBeta = std::lgamma(alpha) + std::lgamma(beta) - std::lgamma(alpha + beta);

    // Compute the log-PDF of the Beta distribution
    double logPDF = (alpha - 1) * std::log(x) + (beta - 1) * std::log(1 - x) - logBeta;

    return logPDF;
}

/****************************************/
/****************************************/

// Assuming actor_net is your Actor Neural Network (an instance of torch::nn::Module)
// Assuming behavior_probs, beta_parameters, chosen_behavior, chosen_parameter, and td_error are already computed

void Homing::update_actor(torch::nn::Module& actor_net, torch::Tensor& behavior_probs, 
                  torch::Tensor& beta_parameters, int chosen_behavior, 
                  double chosen_parameter, double td_error, 
                  torch::optim::Optimizer& optimizer) {

    // Compute log-probability of chosen behavior
    auto log_prob_behavior = torch::log(behavior_probs[chosen_behavior]);

    // Compute parameters for Beta distribution
    auto alpha = beta_parameters[chosen_behavior][0].item<double>();
    auto beta = beta_parameters[chosen_behavior][1].item<double>();

    // Compute log-PDF of Beta distribution at the chosen parameter
    auto log_pdf_beta = computeBetaLogPDF(alpha, beta, chosen_parameter);

    // Compute the surrogate objective
    auto loss = - (log_prob_behavior + log_pdf_beta) * td_error;  // The minus sign because we want to do gradient ascent

    // Zero gradients
    optimizer.zero_grad();

    // Compute gradients w.r.t. actor network parameters (weight and biases)
    loss.backward();

    // Perform one step of the optimization (gradient descent)
    optimizer.step();
}


/****************************************/
/****************************************/

void Homing::print_grid(at::Tensor grid){
    // Get the size of the grid tensor
    auto sizes = grid.sizes();
    int rows = sizes[0];
    int cols = sizes[1];
    int dimension = std::max(rows, cols);

    // Calculate the scaling factors for rows and columns to achieve a square aspect ratio
    float scale_row = static_cast<float>(dimension) / rows;
    float scale_col = static_cast<float>(dimension) / cols;

    // Print the grid with layout
    for (int y = 0; y < dimension; ++y) {
        if (y % int(scale_row) == 0) {
            // Print the top and bottom grid borders
            for (int x = 0; x < dimension; ++x) {
                std::cout << "+---";
            }
            std::cout << "+" << std::endl;
        }

        for (int x = 0; x < dimension; ++x) {
            if (x % int(scale_col) == 0) {
                std::cout << "|"; // Print the left grid border
            }

            // Calculate the corresponding position in the original grid
            int orig_x = static_cast<int>(x / scale_col);
            int orig_y = static_cast<int>(y / scale_row);

            // Check if the current position is within the original grid bounds
            if (orig_x < cols && orig_y < rows) {
                float value = grid[orig_y][orig_x].item<float>();

                // Print highlighted character if the value is 1, otherwise print a space
                if (value == 1) {
                    std::cout << " R ";
		} else if (value == 2){
		    std::cout << " C ";
		} else if (value == 3){
                    std::cout << " B ";
                } else {
                    std::cout << "   ";
                }
            } else {
                std::cout << " "; // Print a space for out-of-bounds positions
            }
        }

        if (y % int(scale_row) == 0) {
            std::cout << "|" << std::endl; // Print the right grid border
        } else {
            std::cout << "|" << std::endl; // Print a separator for other rows
        }
    }

    // Print the bottom grid border
    for (int x = 0; x < dimension; ++x) {
        std::cout << "+---";
    }
    std::cout << "+" << std::endl;
}

/****************************************/
/****************************************/
REGISTER_LOOP_FUNCTIONS(Homing, "homing");
