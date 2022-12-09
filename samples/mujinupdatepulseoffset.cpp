// -*- coding: utf-8 -*-
/** \example mujincreateikparam.cpp

    Shows how to create ikparam using current coordinate
    example: mujincreateikparam --controller_hostname localhost --task_scenepk machine.mujin.dae --iktype Transform6D --object_name object --taskparameters '{"robotname":"robot","toolname":"tool"}'
 */

#include <mujincontrollerclient/binpickingtask.h>
#include <iostream>
#include <cmath>

#if defined(_WIN32) || defined(_WIN64)
#undef GetUserName // clashes with ControllerClient::GetUserName
#endif // defined(_WIN32) || defined(_WIN64)


using namespace mujinclient;
namespace mujinjson = mujinclient::mujinjson_external;

#include <boost/program_options.hpp>
#include <boost/bind.hpp>

namespace bpo = boost::program_options;
using namespace std;

/// \brief parse command line options and store in a map
/// \param argc number of arguments
/// \param argv arguments
/// \param opts map where parsed options are stored
/// \return true if non-help options are parsed succesfully.
bool ParseOptions(int argc, char ** argv, bpo::variables_map& opts)
{
    // parse command line arguments
    bpo::options_description desc("Options");

    desc.add_options()
        ("help,h", "produce help message")
        ("controller_hostname", bpo::value<string>()->required(), "hostname or ip of the mujin controller, e.g. controllerXX or 192.168.0.1")
        ("controller_port", bpo::value<unsigned int>()->default_value(80), "port of the mujin controller")
        ("slave_request_id", bpo::value<string>()->default_value(""), "request id of the mujin slave, e.g. controller20_slave0. If empty, uses  ")
        ("controller_username_password", bpo::value<string>()->default_value("testuser:pass"), "username and password to the mujin controller, e.g. username:password")
        ("controller_command_timeout", bpo::value<double>()->default_value(10), "command timeout in seconds, e.g. 10")
        ("locale", bpo::value<string>()->default_value("en_US"), "locale to use for the mujin controller client")
        ("task_scenepk", bpo::value<string>()->default_value(""), "scene pk of the binpicking task on the mujin controller, e.g. officeboltpicking.mujin.dae.")
        ("taskparameters", bpo::value<string>()->default_value("{}"), "binpicking task parameters, e.g. {\"robotname\": \"robot\", \"toolname\": \"tool\"}")
        ("zmq_port", bpo::value<unsigned int>()->default_value(11000), "port of the binpicking task on the mujin controller")
        ("heartbeat_port", bpo::value<unsigned int>()->default_value(11001), "port of the binpicking task's heartbeat signal on the mujin controller")
        ("robot_name", bpo::value<string>()->default_value(""), "object name to create ikparam for")
        ("update_external_reference", bpo::value<unsigned int>()->default_value(0), "if 1, update external reference")
        ;

    try {
        bpo::store(bpo::parse_command_line(argc, argv, desc, bpo::command_line_style::unix_style ^ bpo::command_line_style::allow_short), opts);
    }
    catch (const exception& ex) {
        stringstream errss;
        errss << "Caught exception " << ex.what();
        cerr << errss.str() << endl;
        return false;
    }

    bool badargs = false;
    try {
        bpo::notify(opts);
    }
    catch(const exception& ex) {
        stringstream errss;
        errss << "Caught exception " << ex.what();
        cerr << errss.str() << endl;
        badargs = true;
    }

    if(opts.count("help") || badargs) {
        cout << "Usage: " << argv[0] << " [OPTS]" << endl;
        cout << endl;
        cout << desc << endl;
        return false;
    }
    return true;
}

/// \brief initialize BinPickingTask and establish communication with controller
/// \param opts options parsed from command line
/// \param pBinPickingTask bin picking task to be initialized
void InitializeTask(const bpo::variables_map& opts,
                    boost::shared_ptr<zmq::context_t>& zmqcontext,
                    BinPickingTaskResourcePtr& pBinpickingTask)
{
    const string controllerUsernamePass = opts["controller_username_password"].as<string>();
    const double controllerCommandTimeout = opts["controller_command_timeout"].as<double>();
    const string taskparameters = opts["taskparameters"].as<string>();
    const string locale = opts["locale"].as<string>();
    const unsigned int taskZmqPort = opts["zmq_port"].as<unsigned int>();
    const string hostname = opts["controller_hostname"].as<string>();
    const unsigned int controllerPort = opts["controller_port"].as<unsigned int>();
    stringstream urlss;
    urlss << "http://" << hostname << ":" << controllerPort;

    const unsigned int heartbeatPort = opts["heartbeat_port"].as<unsigned int>();
    string slaverequestid = opts["slave_request_id"].as<string>();
    string taskScenePk = opts["task_scenepk"].as<string>();
    
    const bool needtoobtainfromheartbeat = taskScenePk.empty() || slaverequestid.empty();
    if (needtoobtainfromheartbeat) {
        stringstream endpoint;
        endpoint << "tcp://" << hostname << ":" << heartbeatPort;
        cout << "connecting to heartbeat at " << endpoint.str() << endl;
        string heartbeat;
        const size_t num_try_heartbeat(10);
        for (size_t it_try_heartbeat = 0; it_try_heartbeat < num_try_heartbeat; ++it_try_heartbeat) {
            heartbeat = utils::GetHeartbeat(endpoint.str());
            if (!heartbeat.empty()) {
                break;
            }
            cout << "Failed to get heart beat " << it_try_heartbeat << "/" << num_try_heartbeat << "\n";
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        }
        if (heartbeat.empty()) {
            throw MujinException(boost::str(boost::format("Failed to obtain heartbeat from %s. Is controller running?")%endpoint.str()));
        }
        
        if (taskScenePk.empty()) {
            taskScenePk = utils::GetScenePkFromHeartbeat(heartbeat);
            cout << "task_scenepk: " << taskScenePk << " is obtained from heartbeat\n";
        }
        if (slaverequestid.empty()) {
            slaverequestid = utils::GetSlaveRequestIdFromHeartbeat(heartbeat);
            cout << "slave_request_id: " << slaverequestid << " is obtained from heartbeat\n";
        }
    }

    //    cout << taskparameters << endl;
    const string tasktype = "realtimeitlplanning3";

    // connect to mujin controller
    ControllerClientPtr controllerclient = CreateControllerClient(controllerUsernamePass, urlss.str());

    cout << "connected to mujin controller at " << urlss.str() << endl;

    SceneResourcePtr scene(new SceneResource(controllerclient, taskScenePk));

    // initialize binpicking task
    pBinpickingTask = scene->GetOrCreateBinPickingTaskFromName_UTF8(tasktype+string("task1"), tasktype, TRO_EnableZMQ);
    const string userinfo = "{\"username\": \"" + controllerclient->GetUserName() + "\", ""\"locale\": \"" + locale + "\"}";
    cout << "initialzing binpickingtask with userinfo=" + userinfo << " taskparameters=" << taskparameters << endl;

    pBinpickingTask->Initialize(taskparameters, taskZmqPort, heartbeatPort, zmqcontext, false, controllerCommandTimeout, controllerCommandTimeout, userinfo, slaverequestid);

}

void ReinitializeTask(boost::shared_ptr<zmq::context_t>& zmqcontext,
                      BinPickingTaskResourcePtr& pBinpickingTask)
{
    const string taskparameters("{\"robotname\": \"robot\"}");
    const unsigned int taskZmqPort(11000);
    const double controllerCommandTimeout(10);
    const string userinfo("");
    const string slaverequestid("controller71_slave0");
    const unsigned int heartbeatPort(11001);
    pBinpickingTask->Initialize(taskparameters, taskZmqPort, heartbeatPort, zmqcontext, false, controllerCommandTimeout, controllerCommandTimeout, userinfo, slaverequestid);
}

int main(int argc, char ** argv)
{
    // parsing options
    bpo::variables_map opts;
    if (!ParseOptions(argc, argv, opts)) {
        // parsing option failed
        return 1;
    }

    // initializing
    BinPickingTaskResourcePtr pBinpickingTask;
    boost::shared_ptr<zmq::context_t> zmqcontext(new zmq::context_t(1));
    InitializeTask(opts, zmqcontext, pBinpickingTask);

    const double timeout = opts["controller_command_timeout"].as<double>();

    BinPickingTaskResource::ResultGetJointValues resultGetJointValues;
    pBinpickingTask->GetJointValues(resultGetJointValues);
    //resultGetJointValues.currentjointvalues;

    // get manip position
    const string object_name = opts["robot_name"].as<string>();

    stringstream urlss;
    {
        const string hostname = opts["controller_hostname"].as<string>();
        const unsigned int controllerPort = opts["controller_port"].as<unsigned int>();
        urlss << "http://" << hostname << ":" << controllerPort;
    }
    ControllerClientPtr controllerclient = CreateControllerClient(opts["controller_username_password"].as<string>(), urlss.str());
    SceneResourcePtr scene(new SceneResource(controllerclient, opts["task_scenepk"].as<string>()));
    std::vector<SceneResource::InstObjectPtr> instobjects;
    scene->GetInstObjects(instobjects);
    std::vector<SceneResource::InstObjectPtr>::iterator it1 = std::find_if(instobjects.begin(),instobjects.end(),boost::bind(&SceneResource::InstObject::name,_1)==object_name);
    std::string object_pk = (*it1)->object_pk;
    if(opts["update_external_reference"].as<unsigned int>() && !(*it1)->reference_object_pk.empty()){
        object_pk = (*it1)->reference_object_pk;
    }
    RobotResourcePtr robot(new RobotResource(controllerclient, object_pk));
    cout << "obtaining robot done" << endl;

    double currentEncoderOffset;
    const int jointIndexJ1 = 0;
    double referenceJ1JointValue = 15.0;
    double encoderMultiplerJ1 = 2349.0;
    robot->GetEncoderOffset(jointIndexJ1, currentEncoderOffset);
    const double newEncoderOffset = ComputeEncoderOffset(resultGetJointValues.currentjointvalues[jointIndexJ1], referenceJ1JointValue, currentEncoderOffset, encoderMultiplerJ1);
    robot->SetEncoderOffset(jointIndexJ1, newEncoderOffset);

    return 0;
}
