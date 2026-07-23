#include "ns3/ai-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

constexpr uint32_t kServiceCount = 3;
constexpr uint32_t kObservationSize = 18;
constexpr uint32_t kActionCount = 4;

struct TrafficProfile
{
    std::string name;
    uint32_t packetSize;
    std::string dataRate;
    double onTime;
    double offTime;
    uint16_t port;
};

struct FlowMetrics
{
    std::string serviceName;
    uint32_t txPackets{0};
    uint32_t rxPackets{0};
    uint32_t lostPackets{0};
    uint64_t rxBytes{0};
    double goodput{0.0};
    double throughput{0.0};
    double averageDelay{0.0};
    double jitter{0.0};
    double packetLoss{0.0};
    double queueOccupancy{0.0};
    double channelUtilization{0.0};
    Ipv4Address source;
    Ipv4Address destination;
};

struct Action
{
    uint32_t allocation{kActionCount - 1};
    double videoWeight{1.0};
    double voiceWeight{1.0};
    double iotWeight{1.0};
};

struct Observation
{
    FlowMetrics video;
    FlowMetrics voice;
    FlowMetrics iot;
    double fairness{0.0};
};

struct Reward
{
    double value{0.0};
};

class Scheduler
{
  public:
    void ApplyAction(uint32_t allocation)
    {
        m_action = Decode(allocation);
    }

    const Action& GetAction() const
    {
        return m_action;
    }

  private:
    static Action Decode(uint32_t allocation)
    {
        Action action;
        action.allocation = allocation < kActionCount ? allocation : kActionCount - 1;
        switch (action.allocation)
        {
        case 0: // Equal allocation
            break;
        case 1: // Video priority
            action.videoWeight = 2.0;
            break;
        case 2: // Voice priority
            action.voiceWeight = 2.0;
            break;
        case 3: // IoT priority
            action.iotWeight = 2.0;
            break;
        }
        return action;
    }

    Action m_action;
};

class RewardCalculator
{
  public:
    Reward Calculate(const Observation& observation) const
    {
        const std::array<FlowMetrics, kServiceCount> flows{
            observation.video, observation.voice, observation.iot};
        double totalGoodput = 0.0;
        double delay = 0.0;
        double loss = 0.0;
        for (const auto& flow : flows)
        {
            totalGoodput += flow.goodput;
            delay += flow.averageDelay;
            loss += flow.packetLoss;
        }

        Reward reward;
        reward.value = totalGoodput + observation.fairness - delay / flows.size() - loss / flows.size();
        return reward;
    }
};

class MetricsCollector
{
  public:
    Observation Collect(Ptr<FlowMonitor> monitor,
                        FlowMonitorHelper& flowmon,
                        const std::array<TrafficProfile, kServiceCount>& traffic,
                        const std::array<Ptr<PacketSink>, kServiceCount>& receivers,
                        double interval)
    {
        monitor->CheckForLostPackets();
        auto classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
        Observation observation;
        std::array<FlowMetrics*, kServiceCount> metrics{
            &observation.video, &observation.voice, &observation.iot};
        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            metrics[i]->serviceName = traffic[i].name;
        }

        const auto stats = monitor->GetFlowStats();
        for (const auto& flow : stats)
        {
            const auto tuple = classifier->FindFlow(flow.first);
            uint32_t index = kServiceCount;
            for (uint32_t i = 0; i < kServiceCount; ++i)
            {
                if (tuple.destinationPort == traffic[i].port)
                {
                    index = i;
                    break;
                }
            }
            if (index == kServiceCount)
            {
                continue;
            }

            const auto& previous = m_previous[flow.first];
            const auto& current = flow.second;
            FlowMetrics& metric = *metrics[index];
            metric.source = tuple.sourceAddress;
            metric.destination = tuple.destinationAddress;
            metric.txPackets = current.txPackets - previous.txPackets;
            metric.rxPackets = current.rxPackets - previous.rxPackets;
            metric.lostPackets = current.lostPackets - previous.lostPackets;
            metric.rxBytes = current.rxBytes - previous.rxBytes;
            if (metric.txPackets > 0)
            {
                metric.packetLoss = 100.0 * metric.lostPackets / metric.txPackets;
            }
            if (metric.rxPackets > 0)
            {
                metric.averageDelay =
                    (current.delaySum - previous.delaySum).GetSeconds() * 1000.0 / metric.rxPackets;
            }
            if (metric.rxPackets > 1)
            {
                metric.jitter =
                    (current.jitterSum - previous.jitterSum).GetSeconds() * 1000.0 / (metric.rxPackets - 1);
            }
            metric.throughput = metric.rxBytes * 8.0 / (interval * 1000000.0);
            // FlowMonitor does not expose the Wi-Fi MAC queue through its portable API.
            // Packets sent in this interval but not yet received or declared lost form a
            // useful per-flow backlog estimate for the RL state.
            metric.queueOccupancy = std::max(
                0.0,
                static_cast<double>(metric.txPackets) - metric.rxPackets - metric.lostPackets);
            m_previous[flow.first] = current;
        }

        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            const uint64_t received = receivers[i]->GetTotalRx();
            const uint64_t delta = received - m_previousRx[i];
            m_previousRx[i] = received;
            metrics[i]->goodput = delta * 8.0 / (interval * 1000000.0);
            metrics[i]->channelUtilization = std::min(1.0, metrics[i]->goodput / 600.0);
        }
        observation.fairness = CalculateFairness({observation.video, observation.voice, observation.iot});
        return observation;
    }

    void Reset()
    {
        m_previous.clear();
        m_previousRx.fill(0);
    }

    void Print(const Observation& observation) const
    {
        const std::array<FlowMetrics, kServiceCount> flows{
            observation.video, observation.voice, observation.iot};
        for (const auto& flow : flows)
        {
            std::cout << "\n========================================\n" << flow.serviceName
                      << "\n========================================\n";
            std::cout << "Tx Packets:      " << flow.txPackets << "\nRx Packets:      " << flow.rxPackets
                      << "\nLost Packets:    " << flow.lostPackets << "\n";
            std::cout << std::fixed << std::setprecision(3) << "Packet Loss:     " << flow.packetLoss
                      << " %\nAverage Delay:   " << flow.averageDelay << " ms\nThroughput:      "
                      << flow.throughput << " Mbps\nGoodput:         " << flow.goodput << " Mbps\n";
        }
        std::cout << "\nJain Fairness: " << std::fixed << std::setprecision(3) << observation.fairness
                  << "\n";
    }

  private:
    static double CalculateFairness(const std::array<FlowMetrics, kServiceCount>& metrics)
    {
        double sum = 0.0;
        double squares = 0.0;
        for (const auto& metric : metrics)
        {
            sum += metric.goodput;
            squares += metric.goodput * metric.goodput;
        }
        return squares > 0.0 ? sum * sum / (metrics.size() * squares) : 0.0;
    }

    std::map<FlowId, FlowMonitor::FlowStats> m_previous;
    std::array<uint64_t, kServiceCount> m_previousRx{};
};

class WifiEnvironment : public OpenGymEnv
{
  public:
    void SetDecisionInterval(double interval)
    {
        NS_ABORT_MSG_IF(interval <= 0.0, "decisionInterval must be positive");
        m_decisionInterval = interval;
    }

    void SetTrafficStop(double stop)
    {
        NS_ABORT_MSG_IF(stop <= m_trafficStart, "trafficStop must be after trafficStart");
        m_trafficStop = stop;
        m_simulationTime = stop + 1.0;
    }

    void Setup()
    {
        CreateNodes();
        ConfigureWifi();
        ConfigureMobility();
        ConfigureInternet();
        InstallApplications();
        m_monitor = m_flowmon.InstallAll();
        SetOpenGymInterface(OpenGymInterface::Get());
        m_initialized = true;
    }

    void Run(bool gymEnabled)
    {
        NS_ABORT_MSG_IF(!m_initialized, "Call Setup before Run");
        if (gymEnabled)
        {
            m_gymEnabled = true;
            Simulator::Schedule(Seconds(m_trafficStart), &WifiEnvironment::NotifyStep, this);
        }
        Simulator::Stop(Seconds(m_simulationTime));
        Simulator::Run();
        CollectMetrics(m_gymEnabled ? m_decisionInterval : m_trafficStop - m_trafficStart);
        m_reward = m_rewardCalculator.Calculate(m_observation);
        if (m_gymEnabled)
        {
            NotifySimulationEnd();
        }
        m_hasRun = true;
    }

    void PrintResults() const
    {
        NS_ABORT_MSG_IF(!m_hasRun, "Call Run before PrintResults");
        m_metrics.Print(m_observation);
        std::cout << "Reward: " << m_reward.value << "\n";
    }

    Ptr<OpenGymSpace> GetActionSpace() override
    {
        return CreateObject<OpenGymDiscreteSpace>(kActionCount);
    }

    Ptr<OpenGymSpace> GetObservationSpace() override
    {
        return CreateObject<OpenGymBoxSpace>(0.0f,
                                             1000000.0f,
                                             std::vector<uint32_t>{kObservationSize},
                                             TypeNameGet<float>());
    }

    bool GetGameOver() override
    {
        return Simulator::Now().GetSeconds() >= m_trafficStop;
    }

    Ptr<OpenGymDataContainer> GetObservation() override
    {
        auto box = CreateObject<OpenGymBoxContainer<float>>(std::vector<uint32_t>{kObservationSize});
        for (const auto& flow : {m_observation.video, m_observation.voice, m_observation.iot})
        {
            box->AddValue(flow.queueOccupancy);
            box->AddValue(flow.throughput);
            box->AddValue(flow.averageDelay);
            box->AddValue(flow.packetLoss);
            box->AddValue(flow.channelUtilization);
            box->AddValue(static_cast<float>(flow.rxPackets));
        }
        return box;
    }

    float GetReward() override
    {
        return static_cast<float>(m_reward.value);
    }

    std::string GetExtraInfo() override
    {
        return "action=" + std::to_string(m_scheduler.GetAction().allocation) +
               ";fairness=" + std::to_string(m_observation.fairness);
    }

    bool ExecuteActions(Ptr<OpenGymDataContainer> action) override
    {
        auto discrete = DynamicCast<OpenGymDiscreteContainer>(action);
        if (!discrete || discrete->GetValue() >= kActionCount)
        {
            return false;
        }
        m_scheduler.ApplyAction(discrete->GetValue());
        UpdateTrafficAllocation();
        return true;
    }

  private:
    void NotifyStep()
    {
        CollectMetrics(m_decisionInterval);
        m_reward = m_rewardCalculator.Calculate(m_observation);
        Notify();
        const double remaining = m_trafficStop - Simulator::Now().GetSeconds();
        if (remaining > 0.0)
        {
            Simulator::Schedule(Seconds(std::min(m_decisionInterval, remaining)),
                                &WifiEnvironment::NotifyStep,
                                this);
        }
    }

    void CreateNodes()
    {
        m_apNode.Create(1);
        m_staNodes.Create(kServiceCount);
    }

    void ConfigureWifi()
    {
        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        phy.Set("TxPowerStart", DoubleValue(m_txPower));
        phy.Set("TxPowerEnd", DoubleValue(m_txPower));
        const Ssid ssid(m_ssidName);
        WifiMacHelper mac;
        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
        m_staDevices = wifi.Install(phy, mac, m_staNodes);
        mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
        m_apDevice = wifi.Install(phy, mac, m_apNode);
    }

    void ConfigureMobility()
    {
        MobilityHelper mobility;
        auto positions = CreateObject<ListPositionAllocator>();
        positions->Add(Vector(0.0, 0.0, 0.0));
        positions->Add(Vector(5.0, 0.0, 0.0));
        positions->Add(Vector(8.0, 0.0, 0.0));
        positions->Add(Vector(12.0, 0.0, 0.0));
        mobility.SetPositionAllocator(positions);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        NodeContainer nodes;
        nodes.Add(m_apNode);
        nodes.Add(m_staNodes);
        mobility.Install(nodes);
    }

    void ConfigureInternet()
    {
        InternetStackHelper stack;
        NodeContainer nodes;
        nodes.Add(m_apNode);
        nodes.Add(m_staNodes);
        stack.Install(nodes);
        Ipv4AddressHelper address;
        address.SetBase("192.168.1.0", "255.255.255.0");
        m_staInterfaces = address.Assign(m_staDevices);
        m_apInterface = address.Assign(m_apDevice);
    }

    void InstallApplications()
    {
        const std::array<Ptr<Node>, kServiceCount> sinks{m_staNodes.Get(0), m_staNodes.Get(1),
                                                           m_apNode.Get(0)};
        const std::array<Ptr<Node>, kServiceCount> sources{m_apNode.Get(0), m_apNode.Get(0),
                                                             m_staNodes.Get(2)};
        const std::array<Ipv4Address, kServiceCount> destinations{
            m_staInterfaces.GetAddress(0), m_staInterfaces.GetAddress(1), m_apInterface.GetAddress(0)};
        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), m_traffic[i].port));
            m_servers[i] = sink.Install(sinks[i]);
            OnOffHelper client("ns3::UdpSocketFactory", InetSocketAddress(destinations[i], m_traffic[i].port));
            client.SetAttribute("PacketSize", UintegerValue(m_traffic[i].packetSize));
            client.SetAttribute("DataRate", DataRateValue(DataRate(m_traffic[i].dataRate)));
            client.SetAttribute("OnTime",
                                StringValue("ns3::ConstantRandomVariable[Constant=" +
                                            std::to_string(m_traffic[i].onTime) + "]"));
            client.SetAttribute("OffTime",
                                StringValue("ns3::ConstantRandomVariable[Constant=" +
                                            std::to_string(m_traffic[i].offTime) + "]"));
            m_clients[i] = client.Install(sources[i]);
            m_servers[i].Start(Seconds(0.0));
            m_servers[i].Stop(Seconds(m_simulationTime));
            m_clients[i].Start(Seconds(m_trafficStart));
            m_clients[i].Stop(Seconds(m_trafficStop));
        }
    }

    void CollectMetrics(double interval)
    {
        std::array<Ptr<PacketSink>, kServiceCount> receivers;
        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            receivers[i] = DynamicCast<PacketSink>(m_servers[i].Get(0));
        }
        m_observation = m_metrics.Collect(m_monitor, m_flowmon, m_traffic, receivers, interval);
    }

    void UpdateTrafficAllocation()
    {
        const Action& action = m_scheduler.GetAction();
        const std::array<double, kServiceCount> weights{
            action.videoWeight, action.voiceWeight, action.iotWeight};
        std::array<double, kServiceCount> baseRates{};
        double offeredLoad = 0.0;
        double weightedLoad = 0.0;
        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            baseRates[i] = DataRate(m_traffic[i].dataRate).GetBitRate();
            offeredLoad += baseRates[i];
            weightedLoad += baseRates[i] * weights[i];
        }

        for (uint32_t i = 0; i < kServiceCount; ++i)
        {
            const uint64_t rate = static_cast<uint64_t>(baseRates[i] * weights[i] *
                                                        offeredLoad / weightedLoad);
            auto application = DynamicCast<OnOffApplication>(m_clients[i].Get(0));
            application->SetAttribute("DataRate", DataRateValue(DataRate(rate)));
        }
    }

    double m_simulationTime{20.0};
    const double m_trafficStart{1.0};
    double m_trafficStop{19.0};
    double m_decisionInterval{0.1};
    const std::string m_ssidName{"WiFi6-Network"};
    const double m_txPower{20.0};
    const std::array<TrafficProfile, kServiceCount> m_traffic{{{"HD Video", 1400, "20Mbps", 1.0, 0.0, 5000},
                                                                  {"Video Call", 200, "1Mbps", 1.0, 0.0, 5001},
                                                                  {"IoT", 64, "50Kbps", 0.05, 2.0, 5002}}};
    NodeContainer m_apNode;
    NodeContainer m_staNodes;
    NetDeviceContainer m_staDevices;
    NetDeviceContainer m_apDevice;
    Ipv4InterfaceContainer m_staInterfaces;
    Ipv4InterfaceContainer m_apInterface;
    std::array<ApplicationContainer, kServiceCount> m_servers;
    std::array<ApplicationContainer, kServiceCount> m_clients;
    FlowMonitorHelper m_flowmon;
    Ptr<FlowMonitor> m_monitor;
    Scheduler m_scheduler;
    MetricsCollector m_metrics;
    RewardCalculator m_rewardCalculator;
    Observation m_observation;
    Reward m_reward;
    bool m_initialized{false};
    bool m_hasRun{false};
    bool m_gymEnabled{false};
};

}

int
main(int argc, char* argv[])
{
    bool gym = false;
    double decisionInterval = 0.1;
    double trafficStop = 19.0;
    CommandLine commandLine(__FILE__);
    commandLine.AddValue("gym", "Enable the ns3-ai Gymnasium shared-memory interface.", gym);
    commandLine.AddValue("decisionInterval", "Gym decision interval in seconds.", decisionInterval);
    commandLine.AddValue("trafficStop", "Traffic stop time in seconds.", trafficStop);
    commandLine.Parse(argc, argv);

    auto env = CreateObject<WifiEnvironment>();
    env->SetDecisionInterval(decisionInterval);
    env->SetTrafficStop(trafficStop);
    env->Setup();
    env->Run(gym);
    if (!gym)
    {
        env->PrintResults();
    }
    Simulator::Destroy();
    return 0;
}
