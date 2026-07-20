#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <array>
#include <cmath>
#include <iomanip>

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

void PrintMetrics(const FlowMetrics& metrics)
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

int main()
{
    const double simulationTime = 20.0;
    const double trafficStart = 1.0;
    const double trafficStop = 19.0;
    const double trafficDuration = trafficStop - trafficStart;

    const std::string ssidName = "WiFi6-Network";
    const double txPower = 20.0;

    const uint16_t videoPort = 5000;
    const uint16_t voicePort = 5001;
    const uint16_t iotPort = 5002;

    const TrafficProfile video{"HD Video", 1400, "20Mbps", 1.0, 0.0, videoPort};
    const TrafficProfile voice{"Video Call", 200, "1Mbps", 1.0, 0.0, voicePort};
    const TrafficProfile iot{"IoT", 64, "50Kbps", 0.05, 2.0, iotPort};

    NodeContainer apNode;
    NodeContainer staNodes;
    apNode.Create(1);
    staNodes.Create(3);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));

    const Ssid ssid(ssidName);
    WifiMacHelper mac;
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0.0, 0.0, 0.0));
    positions->Add(Vector(5.0, 0.0, 0.0));
    positions->Add(Vector(8.0, 0.0, 0.0));
    positions->Add(Vector(12.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    NodeContainer allNodes;
    allNodes.Add(apNode);
    allNodes.Add(staNodes);
    mobility.Install(allNodes);

    InternetStackHelper stack;
    stack.Install(allNodes);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);

    std::cout << "========================================\n";
    std::cout << "AP     : " << apInterface.GetAddress(0) << "\n";
    std::cout << "Video  : " << staInterfaces.GetAddress(0) << "\n";
    std::cout << "Voice  : " << staInterfaces.GetAddress(1) << "\n";
    std::cout << "IoT    : " << staInterfaces.GetAddress(2) << "\n";
    std::cout << "========================================\n";

    PacketSinkHelper videoSink("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), video.port));
    ApplicationContainer videoServer = videoSink.Install(staNodes.Get(0));
    PacketSinkHelper voiceSink("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), voice.port));
    ApplicationContainer voiceServer = voiceSink.Install(staNodes.Get(1));
    PacketSinkHelper iotSink("ns3::UdpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), iot.port));
    ApplicationContainer iotServer = iotSink.Install(apNode.Get(0));

    OnOffHelper videoClient("ns3::UdpSocketFactory",
                            InetSocketAddress(staInterfaces.GetAddress(0), video.port));
    videoClient.SetAttribute("PacketSize", UintegerValue(video.packetSize));
    videoClient.SetAttribute("DataRate", DataRateValue(DataRate(video.dataRate)));
    videoClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    videoClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer videoApp = videoClient.Install(apNode.Get(0));

    OnOffHelper voiceClient("ns3::UdpSocketFactory",
                            InetSocketAddress(staInterfaces.GetAddress(1), voice.port));
    voiceClient.SetAttribute("PacketSize", UintegerValue(voice.packetSize));
    voiceClient.SetAttribute("DataRate", DataRateValue(DataRate(voice.dataRate)));
    voiceClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    voiceClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer voiceApp = voiceClient.Install(apNode.Get(0));

    OnOffHelper iotClient("ns3::UdpSocketFactory",
                          InetSocketAddress(apInterface.GetAddress(0), iot.port));
    iotClient.SetAttribute("PacketSize", UintegerValue(iot.packetSize));
    iotClient.SetAttribute("DataRate", DataRateValue(DataRate(iot.dataRate)));
    iotClient.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=0.05]"));
    iotClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=2]"));
    ApplicationContainer iotApp = iotClient.Install(staNodes.Get(2));

    videoServer.Start(Seconds(0.0));
    voiceServer.Start(Seconds(0.0));
    iotServer.Start(Seconds(0.0));
    videoServer.Stop(Seconds(simulationTime));
    voiceServer.Stop(Seconds(simulationTime));
    iotServer.Stop(Seconds(simulationTime));
    videoApp.Start(Seconds(trafficStart));
    voiceApp.Start(Seconds(trafficStart));
    iotApp.Start(Seconds(trafficStart));
    videoApp.Stop(Seconds(trafficStop));
    voiceApp.Stop(Seconds(trafficStop));
    iotApp.Stop(Seconds(trafficStop));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    const std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    FlowMetrics videoMetrics{video.name};
    FlowMetrics voiceMetrics{voice.name};
    FlowMetrics iotMetrics{iot.name};

    for (const auto& flow : stats)
    {
        const Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
        FlowMetrics* metrics = nullptr;
        if (tuple.destinationPort == video.port)
        {
            metrics = &videoMetrics;
        }
        else if (tuple.destinationPort == voice.port)
        {
            metrics = &voiceMetrics;
        }
        else if (tuple.destinationPort == iot.port)
        {
            metrics = &iotMetrics;
        }
        else
        {
            continue;
        }

        metrics->source = tuple.sourceAddress;
        metrics->destination = tuple.destinationAddress;
        metrics->txPackets = flow.second.txPackets;
        metrics->rxPackets = flow.second.rxPackets;
        metrics->lostPackets = flow.second.lostPackets;
        metrics->rxBytes = flow.second.rxBytes;
        if (metrics->txPackets > 0)
        {
            metrics->packetLoss = 100.0 * metrics->lostPackets / metrics->txPackets;
        }
        if (metrics->rxPackets > 0)
        {
            metrics->averageDelay =
                flow.second.delaySum.GetSeconds() * 1000.0 / metrics->rxPackets;
        }
        if (metrics->rxPackets > 1)
        {
            metrics->jitter =
                flow.second.jitterSum.GetSeconds() * 1000.0 / (metrics->rxPackets - 1);
        }
        metrics->throughput = metrics->rxBytes * 8.0 / (trafficDuration * 1000000.0);
    }

    Ptr<PacketSink> videoReceiver = DynamicCast<PacketSink>(videoServer.Get(0));
    Ptr<PacketSink> voiceReceiver = DynamicCast<PacketSink>(voiceServer.Get(0));
    Ptr<PacketSink> iotReceiver = DynamicCast<PacketSink>(iotServer.Get(0));
    videoMetrics.goodput = videoReceiver->GetTotalRx() * 8.0 / (trafficDuration * 1000000.0);
    voiceMetrics.goodput = voiceReceiver->GetTotalRx() * 8.0 / (trafficDuration * 1000000.0);
    iotMetrics.goodput = iotReceiver->GetTotalRx() * 8.0 / (trafficDuration * 1000000.0);

    PrintMetrics(videoMetrics);
    PrintMetrics(voiceMetrics);
    PrintMetrics(iotMetrics);

    const std::array<FlowMetrics, 3> metrics{videoMetrics, voiceMetrics, iotMetrics};
    double totalGoodput = 0.0;
    double delaySum = 0.0;
    double lossSum = 0.0;
    double goodputSquares = 0.0;
    for (const auto& metric : metrics)
    {
        totalGoodput += metric.goodput;
        delaySum += metric.averageDelay;
        lossSum += metric.packetLoss;
        goodputSquares += metric.goodput * metric.goodput;
    }
    const double jainFairness = goodputSquares > 0.0 ? totalGoodput * totalGoodput / (metrics.size() * goodputSquares): 0.0;

    std::cout << "\n========================================\n";
    std::cout << "Overall Network\n";
    std::cout << "========================================\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total Goodput:       " << totalGoodput << " Mbps\n";
    std::cout << "Average Delay:       " << delaySum / metrics.size() << " ms\n";
    std::cout << "Average Packet Loss: " << lossSum / metrics.size() << " %\n";
    std::cout << "Jain Fairness:       " << jainFairness << "\n";

    Simulator::Destroy();
    return 0;
}
