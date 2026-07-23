#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <array>
#include <iomanip>
#include <map>
#include <string>

using namespace ns3;

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
    Ipv4Address source;
    Ipv4Address destination;
};

//action supplied by an RL agent. Weights are stored only until a Wi-Fi scheduler is added
struct Action
{
    double videoWeight{1.0};
    double voiceWeight{1.0};
    double iotWeight{1.0};
};

//network state returned to an RL agent after a simulation episode or decision interval
struct Observation
{
    FlowMetrics video;
    FlowMetrics voice;
    FlowMetrics iot;
    double fairness{0.0};
};

//scalar feedback returned to an RL agent
struct Reward
{
    double value{0.0};
};

namespace rl
{

class Scheduler
{
  public:
    void ApplyAction(const Action& action)
    {
        m_action = action;
    }

    const Action& GetAction() const
    {
        return m_action;
    }

  private:
    Action m_action;
};

}

class RewardCalculator
{
  public:
    Reward Calculate(const Observation& observation) const
    {
        // This baseline reward is intentionally independent of scheduler actions.
        // It provides a stable interface while preserving the original simulation behaviour.
        const std::array<FlowMetrics, 3> flows{
            observation.video, observation.voice, observation.iot};
        double totalGoodput = 0.0;
        double averageDelay = 0.0;
        double averageLoss = 0.0;
        for (const auto& flow : flows)
        {
            totalGoodput += flow.goodput;
            averageDelay += flow.averageDelay;
            averageLoss += flow.packetLoss;
        }

        Reward reward;
        reward.value = totalGoodput + observation.fairness - averageDelay / flows.size() -
                       averageLoss / flows.size();
        return reward;
    }
};

class MetricsCollector
{
  public:
    Observation Collect(Ptr<FlowMonitor> monitor,
                        FlowMonitorHelper& flowmon,
                        const std::array<TrafficProfile, 3>& traffic,
                        const std::array<Ptr<PacketSink>, 3>& receivers,
                        double trafficDuration) const
    {
        monitor->CheckForLostPackets();
        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

        Observation observation;
        observation.video.serviceName = traffic[0].name;
        observation.voice.serviceName = traffic[1].name;
        observation.iot.serviceName = traffic[2].name;
        std::array<FlowMetrics*, 3> metrics{
            &observation.video, &observation.voice, &observation.iot};

        const std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
        for (const auto& flow : stats)
        {
            const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
            FlowMetrics* metric = nullptr;
            for (uint32_t i = 0; i < traffic.size(); ++i)
            {
                if (tuple.destinationPort == traffic[i].port)
                {
                    metric = metrics[i];
                    break;
                }
            }
            if (metric == nullptr)
            {
                continue;
            }

            metric->source = tuple.sourceAddress;
            metric->destination = tuple.destinationAddress;
            metric->txPackets = flow.second.txPackets;
            metric->rxPackets = flow.second.rxPackets;
            metric->lostPackets = flow.second.lostPackets;
            metric->rxBytes = flow.second.rxBytes;
            if (metric->txPackets > 0)
            {
                metric->packetLoss = 100.0 * metric->lostPackets / metric->txPackets;
            }
            if (metric->rxPackets > 0)
            {
                metric->averageDelay =
                    flow.second.delaySum.GetSeconds() * 1000.0 / metric->rxPackets;
            }
            if (metric->rxPackets > 1)
            {
                metric->jitter =
                    flow.second.jitterSum.GetSeconds() * 1000.0 / (metric->rxPackets - 1);
            }
            metric->throughput = metric->rxBytes * 8.0 / (trafficDuration * 1000000.0);
        }

        for (uint32_t i = 0; i < metrics.size(); ++i)
        {
            metrics[i]->goodput = receivers[i]->GetTotalRx() * 8.0 / (trafficDuration * 1000000.0);
        }
        observation.fairness = CalculateFairness({observation.video, observation.voice, observation.iot});
        return observation;
    }

    void Print(const Observation& observation) const
    {
        PrintFlow(observation.video);
        PrintFlow(observation.voice);
        PrintFlow(observation.iot);

        const std::array<FlowMetrics, 3> metrics{
            observation.video, observation.voice, observation.iot};
        double totalGoodput = 0.0;
        double delaySum = 0.0;
        double lossSum = 0.0;
        for (const auto& metric : metrics)
        {
            totalGoodput += metric.goodput;
            delaySum += metric.averageDelay;
            lossSum += metric.packetLoss;
        }

        std::cout << "\n========================================\n";
        std::cout << "Overall Network\n";
        std::cout << "========================================\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Total Goodput:       " << totalGoodput << " Mbps\n";
        std::cout << "Average Delay:       " << delaySum / metrics.size() << " ms\n";
        std::cout << "Average Packet Loss: " << lossSum / metrics.size() << " %\n";
        std::cout << "Jain Fairness:       " << observation.fairness << "\n";
    }

  private:
    static double CalculateFairness(const std::array<FlowMetrics, 3>& metrics)
    {
        double totalGoodput = 0.0;
        double goodputSquares = 0.0;
        for (const auto& metric : metrics)
        {
            totalGoodput += metric.goodput;
            goodputSquares += metric.goodput * metric.goodput;
        }
        return goodputSquares > 0.0
                   ? totalGoodput * totalGoodput / (metrics.size() * goodputSquares)
                   : 0.0;
    }

    static void PrintFlow(const FlowMetrics& metrics)
    {
        std::cout << "\n========================================\n";
        std::cout << metrics.serviceName << "\n";
        std::cout << "========================================\n";
        std::cout << "Source:          " << metrics.source << "\n";
        std::cout << "Destination:     " << metrics.destination << "\n";
        std::cout << "Tx Packets:      " << metrics.txPackets << "\n";
        std::cout << "Rx Packets:      " << metrics.rxPackets << "\n";
        std::cout << "Lost Packets:    " << metrics.lostPackets << "\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Packet Loss:     " << metrics.packetLoss << " %\n";
        std::cout << "Average Delay:   " << metrics.averageDelay << " ms\n";
        std::cout << "Average Jitter:  " << metrics.jitter << " ms\n";
        std::cout << "Throughput:      " << metrics.throughput << " Mbps\n";
        std::cout << "Goodput:         " << metrics.goodput << " Mbps\n";
    }
};

class WifiEnvironment
{
  public:
    void Initialize()
    {
        CreateNodes();
        ConfigureWifi();
        ConfigureMobility();
        ConfigureInternet();
        InstallApplications();
        m_monitor = m_flowmon.InstallAll();
        m_initialized = true;
    }

    void ApplyAction(const Action& action)
    {
        m_scheduler.ApplyAction(action);
    }

    void Run()
    {
        NS_ABORT_MSG_IF(!m_initialized, "Call Initialize before Run");
        Simulator::Stop(Seconds(m_simulationTime));
        Simulator::Run();
        CollectMetrics();
        m_reward = m_rewardCalculator.Calculate(m_observation);
        m_hasRun = true;
    }

    void PrintResults() const
    {
        NS_ABORT_MSG_IF(!m_hasRun, "Call Run before PrintResults");
        m_metrics.Print(m_observation);
    }

    const Observation& GetObservation() const
    {
        return m_observation;
    }

    const Reward& GetReward() const
    {
        return m_reward;
    }

    ~WifiEnvironment()
    {
        Simulator::Destroy();
    }

  private:
    void CreateNodes()
    {
        m_apNode.Create(1);
        m_staNodes.Create(3);
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
        Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
        positions->Add(Vector(0.0, 0.0, 0.0));
        positions->Add(Vector(5.0, 0.0, 0.0));
        positions->Add(Vector(8.0, 0.0, 0.0));
        positions->Add(Vector(12.0, 0.0, 0.0));
        mobility.SetPositionAllocator(positions);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

        NodeContainer allNodes;
        allNodes.Add(m_apNode);
        allNodes.Add(m_staNodes);
        mobility.Install(allNodes);
    }

    void ConfigureInternet()
    {
        InternetStackHelper stack;
        NodeContainer allNodes;
        allNodes.Add(m_apNode);
        allNodes.Add(m_staNodes);
        stack.Install(allNodes);

        Ipv4AddressHelper address;
        address.SetBase("192.168.1.0", "255.255.255.0");
        m_staInterfaces = address.Assign(m_staDevices);
        m_apInterface = address.Assign(m_apDevice);

        std::cout << "========================================\n";
        std::cout << "AP     : " << m_apInterface.GetAddress(0) << "\n";
        std::cout << "Video  : " << m_staInterfaces.GetAddress(0) << "\n";
        std::cout << "Voice  : " << m_staInterfaces.GetAddress(1) << "\n";
        std::cout << "IoT    : " << m_staInterfaces.GetAddress(2) << "\n";
        std::cout << "========================================\n";
    }

    void InstallApplications()
    {
        PacketSinkHelper videoSink("ns3::UdpSocketFactory",
                                   InetSocketAddress(Ipv4Address::GetAny(), m_traffic[0].port));
        m_servers[0] = videoSink.Install(m_staNodes.Get(0));
        PacketSinkHelper voiceSink("ns3::UdpSocketFactory",
                                   InetSocketAddress(Ipv4Address::GetAny(), m_traffic[1].port));
        m_servers[1] = voiceSink.Install(m_staNodes.Get(1));
        PacketSinkHelper iotSink("ns3::UdpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), m_traffic[2].port));
        m_servers[2] = iotSink.Install(m_apNode.Get(0));

        InstallOnOffApplication(0, m_apNode.Get(0), m_staInterfaces.GetAddress(0), "1", "0");
        InstallOnOffApplication(1, m_apNode.Get(0), m_staInterfaces.GetAddress(1), "1", "0");
        InstallOnOffApplication(2, m_staNodes.Get(2), m_apInterface.GetAddress(0), "0.05", "2");

        for (uint32_t i = 0; i < m_servers.size(); ++i)
        {
            m_servers[i].Start(Seconds(0.0));
            m_servers[i].Stop(Seconds(m_simulationTime));
            m_clients[i].Start(Seconds(m_trafficStart));
            m_clients[i].Stop(Seconds(m_trafficStop));
        }
    }

    void InstallOnOffApplication(uint32_t trafficIndex,
                                 Ptr<Node> source,
                                 Ipv4Address destination,
                                 const std::string& onTime,
                                 const std::string& offTime)
    {
        const TrafficProfile& profile = m_traffic[trafficIndex];
        OnOffHelper client("ns3::UdpSocketFactory", InetSocketAddress(destination, profile.port));
        client.SetAttribute("PacketSize", UintegerValue(profile.packetSize));
        client.SetAttribute("DataRate", DataRateValue(DataRate(profile.dataRate)));
        client.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + onTime + "]"));
        client.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + offTime + "]"));
        m_clients[trafficIndex] = client.Install(source);
    }

    void CollectMetrics()
    {
        std::array<Ptr<PacketSink>, 3> receivers;
        for (uint32_t i = 0; i < receivers.size(); ++i)
        {
            receivers[i] = DynamicCast<PacketSink>(m_servers[i].Get(0));
        }
        m_observation =
            m_metrics.Collect(m_monitor, m_flowmon, m_traffic, receivers, m_trafficStop - m_trafficStart);
    }

    const double m_simulationTime{20.0};
    const double m_trafficStart{1.0};
    const double m_trafficStop{19.0};
    const std::string m_ssidName{"WiFi6-Network"};
    const double m_txPower{20.0};
    const std::array<TrafficProfile, 3> m_traffic{{{"HD Video", 1400, "20Mbps", 1.0, 0.0, 5000},
                                                     {"Video Call", 200, "1Mbps", 1.0, 0.0, 5001},
                                                     {"IoT", 64, "50Kbps", 0.05, 2.0, 5002}}};

    NodeContainer m_apNode;
    NodeContainer m_staNodes;
    NetDeviceContainer m_staDevices;
    NetDeviceContainer m_apDevice;
    Ipv4InterfaceContainer m_staInterfaces;
    Ipv4InterfaceContainer m_apInterface;
    std::array<ApplicationContainer, 3> m_servers;
    std::array<ApplicationContainer, 3> m_clients;
    FlowMonitorHelper m_flowmon;
    Ptr<FlowMonitor> m_monitor;
    rl::Scheduler m_scheduler;
    MetricsCollector m_metrics;
    RewardCalculator m_rewardCalculator;
    Observation m_observation;
    Reward m_reward;
    bool m_initialized{false};
    bool m_hasRun{false};
};

int main()
{
    WifiEnvironment env;
    env.Initialize();
    env.Run();
    env.PrintResults();
    return 0;
}
