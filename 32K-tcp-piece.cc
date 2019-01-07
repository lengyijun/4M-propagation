/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <ctime>
#include <sstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-nix-vector-helper.h"
#include "ns3/topology-read-module.h"
#include <list>
#include "wifi-example-apps.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TopologyCreationExperiment");

const long totalTxBytes=32*1024;           //piece size
const long totaltotalTxBytes=4*1024*1024;   //4M
const int count=totaltotalTxBytes/totalTxBytes;
const uint16_t listenPort=12345;
const long writeSize=536;  //MTU

void SendStuffWithTag (Ptr<Node> n, Ipv4Address dstaddr,TimestampTag tag);
void BindSock (Ptr<Socket> sock, Ptr<NetDevice> netdev);
void dstSocketRecv (Ptr<Socket> socket);
void HandleAccept (Ptr<Socket> s, const Address& from);
void WriteUntilBufferFull (Ptr<Socket>, uint32_t);
void Init(Ptr<Node> n, Ipv4Address dstaddr);
void SendOnePacketWithTag(Ptr<Node> n,Ipv4Address destination,int toWriteSize,TimestampTag TypeTag,TimestampTag BlockTag);

std::map<uint32_t,std::vector<Ipv4Address>> ipv4NeighMap;
std::map<uint32_t,std::vector<TimestampTag>> tagMap;
std::map<Ptr<Socket>,uint32_t> byteCountMap;
std::map<Ptr<Socket>,TimestampTag> socketTagMap;

TimestampTag FinTag;   //tag the last piece of the 4M bulk
TimestampTag ReqTag;   //request the latest bulk
TimestampTag PaymentTag;
TimestampTag RcvPaymentTag;
TimestampTag InvTag;

const int GetPieceSize=37;
const int InvSize=37;
const int PaymentSize=250;
const int RcvPaymentSize=32;

void SetFinTag();
void SetReqTag();
void SetPaymentTag();
void SetRcvPaymentTag();
void SetInvTag();

int main (int argc, char *argv[])
{
  NS_ASSERT(count>0);

  std::string format ("Inet");
  // std::string input ("src/topology-read/examples/double.txt");          //2
  // std::string input ("src/topology-read/examples/triple.txt");          //3
 // std::string input ("src/topology-read/examples/tree.txt");          //10
  std::string input ("src/topology-read/examples/100ms.txt");        //1000
  // std::string input ("src/topology-read/examples/Inet_toposample.txt");  //4000

  // Set up command line parameters used to control the experiment.
  CommandLine cmd;
  cmd.AddValue ("format", "Format to use for data input [Orbis|Inet|Rocketfuel].",
                format);
  cmd.AddValue ("input", "Name of the input file.",
                input);
  cmd.Parse (argc, argv);

  // ------------------------------------------------------------
  // -- Read topology data.
  // --------------------------------------------

  // Pick a topology reader based in the requested format.
  TopologyReaderHelper topoHelp;
  topoHelp.SetFileName (input);
  topoHelp.SetFileType (format);
  Ptr<TopologyReader> inFile = topoHelp.GetTopologyReader ();

  NodeContainer nodes;

  if (inFile != 0)
    {
      nodes = inFile->Read ();
    }

  if (inFile->LinksSize () == 0)
    {
      NS_LOG_ERROR ("Problems reading the topology file. Failing.");
      return -1;
    }

  // ------------------------------------------------------------
  // -- Create nodes and network stacks
  // --------------------------------------------
  // NS_LOG_INFO ("creating internet stack");
  InternetStackHelper stack;

  // Setup NixVector Routing
  // Ipv4NixVectorHelper nixRouting;
  // stack.SetRoutingHelper (nixRouting);  // has effect on the next Install ()
  stack.Install (nodes);

  // NS_LOG_INFO ("creating ip4 addresses");
  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.252");

  int totlinks = inFile->LinksSize ();

  // NS_LOG_INFO ("creating node containers");
  NodeContainer* nc = new NodeContainer[totlinks];
  std::string* delay=new std::string[totlinks];
  TopologyReader::ConstLinksIterator iter;
  int i = 0;
  for ( iter = inFile->LinksBegin (); iter != inFile->LinksEnd (); iter++, i++ )
    {
      nc[i] = NodeContainer (iter->GetFromNode (), iter->GetToNode ());
      delay[i]=iter->GetAttribute("Weight")+"ms";
    }

  // NS_LOG_INFO ("creating net device containers");
  NetDeviceContainer* ndc = new NetDeviceContainer[totlinks];
  PointToPointHelper p2p;
  for (int i = 0; i < totlinks; i++)
    {
      p2p.SetChannelAttribute ("Delay",StringValue(delay[i]));
      p2p.SetDeviceAttribute ("DataRate", StringValue ("70Mbps"));

     Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
     em->SetAttribute ("ErrorRate", DoubleValue (0.00001));
     p2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (em));

      ndc[i] = p2p.Install (nc[i]);
    }
  delete[] delay;

  // it crates little subnets, one for each couple of nodes.
  // NS_LOG_INFO ("creating ipv4 interfaces");
  Ipv4InterfaceContainer* ipic = new Ipv4InterfaceContainer[totlinks];
  for (int i = 0; i < totlinks; i++)
    {
      ipic[i] = address.Assign (ndc[i]);
      address.NewNetwork ();
    }

  // NS_LOG_INFO ("creating ipv4NeighMap");
  i=0;
  for ( iter = inFile->LinksBegin (); iter != inFile->LinksEnd (); iter++, i++ )
    {
      uint32_t from=iter->GetFromNode()->GetId();
      uint32_t to=iter->GetToNode()->GetId();

      std::map<uint32_t,std::vector<Ipv4Address>>::iterator it= ipv4NeighMap.find(from);
      if (it==ipv4NeighMap.end()){
        std::vector<Ipv4Address> v;
        v.push_back(ipic[i].GetAddress(1));
        ipv4NeighMap.insert(std::make_pair(from,v));
      }else{
        ipv4NeighMap[from].push_back(ipic[i].GetAddress(1));
      }

      std::map<uint32_t,std::vector<Ipv4Address>>::iterator it1= ipv4NeighMap.find(to);
      if (it1==ipv4NeighMap.end()){
        std::vector<Ipv4Address> v;
        v.push_back(ipic[i].GetAddress(0));
        ipv4NeighMap.insert(std::make_pair(to,v));
      }else{
        ipv4NeighMap[to].push_back(ipic[i].GetAddress(0));
      }
    }


  InetSocketAddress dst = InetSocketAddress (Ipv4Address::GetAny(),listenPort);

  for ( unsigned int i = 0; i < nodes.GetN (); i++ )
  {
    Ptr<Socket> dstSocket = Socket::CreateSocket (nodes.Get(i), TypeId::LookupByName ("ns3::TcpSocketFactory"));
    dstSocket->SetAttribute("RcvBufSize", UintegerValue(totalTxBytes));

    dstSocket->Bind (dst);
    dstSocket->Listen();
    dstSocket->SetAcceptCallback (
       MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
       MakeCallback (&HandleAccept)
    );
  }

  // AnimationInterface anim ("animation.xml");
  // p2p.EnablePcapAll ("tcp-socket-piece-200ms");

  Simulator::Schedule(Seconds(0.01),&SetReqTag);
  Simulator::Schedule(Seconds(0.02),&SetFinTag);
  Simulator::Schedule(Seconds(0.03),&SetPaymentTag);
  Simulator::Schedule(Seconds(0.04),&SetRcvPaymentTag);
  Simulator::Schedule(Seconds(0.05),&SetInvTag);

  for(int i=0;i<count;i++){
    Simulator::Schedule(Seconds(0.1+0.0001*i),&Init,nodes.Get(0),ipic[0].GetAddress(1));
  }
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  Simulator::Destroy ();

  delete[] ipic;
  delete[] ndc;
  delete[] nc;

  NS_LOG_INFO ("Done.");

  return 0;
}

void SetInvTag(){
  InvTag.SetTimestamp (Simulator::Now ());
}

void SetPaymentTag(){
  PaymentTag.SetTimestamp (Simulator::Now ());
}

void SetRcvPaymentTag(){
  RcvPaymentTag.SetTimestamp(Simulator::Now());
  NS_ASSERT (!RcvPaymentTag.Equal(PaymentTag));
  //too much I don't know how to assert so much;
}

void SetReqTag(){
  ReqTag.SetTimestamp (Simulator::Now ());
}

void SetFinTag(){
  FinTag.SetTimestamp (Simulator::Now ());
  NS_ASSERT (!FinTag.Equal(ReqTag));
}

void  Init( Ptr<Node> n,Ipv4Address dstaddr)
{

  TimestampTag initBlockTag;
  initBlockTag.SetTimestamp (Simulator::Now ());
  NS_ASSERT(!initBlockTag.Equal(ReqTag));
  NS_ASSERT(!initBlockTag.Equal(FinTag));

  SendOnePacketWithTag(n,dstaddr,InvSize,InvTag,initBlockTag);
  return;
}

void HandleAccept (Ptr<Socket> s, const Address& from)
 {
    // NS_LOG_INFO("HandleAccept");
    s->SetRecvCallback (MakeCallback (&dstSocketRecv));
  }


void SendStuffWithTag(Ptr<Node> n, Ipv4Address dstaddr,TimestampTag tag)
{
  Ptr<Socket> sendSocket = Socket::CreateSocket (n, TypeId::LookupByName ("ns3::TcpSocketFactory"));
  sendSocket->SetAttribute("SndBufSize", UintegerValue(totalTxBytes));  
  sendSocket->SetSendCallback (MakeCallback (&WriteUntilBufferFull));
  sendSocket->Connect (InetSocketAddress (dstaddr,listenPort)); 

  socketTagMap.insert(std::make_pair(sendSocket,tag));
  WriteUntilBufferFull (sendSocket,sendSocket->GetTxAvailable ());

  return;
}

void BindSock (Ptr<Socket> sock, Ptr<NetDevice> netdev)
{
  sock->BindToNetDevice (netdev);
  return;
}

void dstSocketRecv (Ptr<Socket> socket)
{
  Address from;
  Ptr<Packet> packet = socket->RecvFrom (from);

  ByteTagIterator it=packet-> GetByteTagIterator() ;
  TimestampTag TypeTag;
  TimestampTag BlockTag;
  //a piece of 4M data, has no tag
  //we don't need to bother it
  if(!it.HasNext()){
    // NS_LOG_INFO("No tag");
    // this socket cannot close!!!
    // if(socket){
      // socket->Close();
    // }
    return;
  }
  it.Next().GetTag(TypeTag);
  it.Next().GetTag(BlockTag);
  NS_ASSERT(!BlockTag.Equal(TypeTag));

  Ipv4Address ipv4from=InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  Ptr<Node> n=socket->GetNode();
  uint32_t id=n->GetId();
  std::vector<Ipv4Address> v=ipv4NeighMap.find(id)->second;

  //FinTag: mean the whole block is received
  if(TypeTag.Equal(FinTag)){
    NS_LOG_INFO(id<<","<<Simulator::Now());

    //broadcast new block
    for(unsigned int i=0;i<v.size();i++){
      if(v[i].IsEqual(ipv4from)){
        continue;  //don't send back
      }
      SendOnePacketWithTag(n,v[i],InvSize,InvTag,BlockTag);
    }
    if(socket){
      socket->Close();
    }
    return;
  }

  if(TypeTag.Equal(PaymentTag)){
    SendOnePacketWithTag(n,ipv4from,RcvPaymentSize,RcvPaymentTag,BlockTag);
    if(socket){
      socket->Close();
    }
    return;
  }
  
  if(TypeTag.Equal(RcvPaymentTag)){
    SendOnePacketWithTag(n,ipv4from,GetPieceSize,ReqTag,BlockTag);
    if(socket){
      socket->Close();
    }
    return;
  }

  //ReqTag:send the whole data back
  if(TypeTag.Equal(ReqTag)){
    SendStuffWithTag (n,ipv4from, BlockTag);
    if(socket){
      socket->Close();
    }
    return;
  }

  if(TypeTag.Equal(InvTag)){
    std::map<uint32_t,std::vector<TimestampTag>>::iterator tagit= tagMap.find(id);
    if(tagit!=tagMap.end()){
      std::vector<TimestampTag> tagvector= tagit->second;
      for(unsigned int i=0;i<tagvector.size();i++){
        if(BlockTag.Equal(tagvector[i])){ 
          //old tag
          //this packet is a header message, and it tell me that the node has a packet which I have already
          //so I'll not bother it
          // NS_LOG_INFO("The "<<id<<" node receive a packet with an old HEADER");
          if(socket){
            socket->Close();
          }
          return;
        }
      }
    }

  tagMap[id].push_back(BlockTag);
  SendOnePacketWithTag(n,ipv4from,PaymentSize,PaymentTag,BlockTag);
  if(socket){
    socket->Close();
  }
  return;
  }
}

void SendOnePacketWithTag(Ptr<Node> n,Ipv4Address destination,int toWriteSize,TimestampTag TypeTag,TimestampTag BlockTag){
  NS_ASSERT(toWriteSize>0);
  NS_ASSERT(!TypeTag.Equal(BlockTag));

  Ptr<Socket> sendSocket = Socket::CreateSocket (n, TypeId::LookupByName ("ns3::TcpSocketFactory"));
  sendSocket->Connect (InetSocketAddress (destination,listenPort)); 
  Ptr<Packet> p = Create<Packet> ();
  p->AddPaddingAtEnd (toWriteSize);
  p->AddByteTag(TypeTag);
  p->AddByteTag(BlockTag);
  sendSocket->Send(p);
  if(sendSocket){
    sendSocket->Close();
  }
  return;
}

void WriteUntilBufferFull (Ptr<Socket> localSocket, uint32_t txSpace)
 {
   std::map<Ptr<Socket>,uint32_t>::iterator it=byteCountMap.find(localSocket);
   if(it==byteCountMap.end()){
     byteCountMap.insert(std::make_pair(localSocket,0));
   }
   while (byteCountMap[localSocket]< totalTxBytes && localSocket->GetTxAvailable () > 0) 
     {
       // NS_LOG_INFO("currentTxBytes: "<<byteCountMap[localSocket]<<";totalTxBytes: "<<totalTxBytes);
       uint32_t left = totalTxBytes -byteCountMap[localSocket];
       uint32_t dataOffset = byteCountMap[localSocket]% writeSize;
       uint32_t toWrite = writeSize - dataOffset;
       toWrite = std::min (toWrite, left);
       toWrite = std::min (toWrite, localSocket->GetTxAvailable ());
       NS_ASSERT(toWrite>0);

       Ptr<Packet> p= Create<Packet> ();
       p->AddPaddingAtEnd (toWrite);
       //the last packet
       if(totalTxBytes==toWrite+byteCountMap[localSocket]){ 
         // NS_LOG_INFO("the last packet");
         std::map<Ptr<Socket>,TimestampTag>::iterator it = socketTagMap.find(localSocket);
         if(it!=socketTagMap.end()){
           p->AddByteTag (FinTag);
           p->AddByteTag (it->second);
         }else{
           NS_LOG_ERROR("CANNOT FOUND SOCKET KEY IN socketTagMap");
         }
       }
       int amountSent = localSocket->Send (p);

       if(amountSent < 0)
       {
         // we will be called again when new tx space becomes available.
         NS_LOG_WARN("amountSent < 0");
         return;
       }
       byteCountMap[localSocket]+= amountSent;
     }
   localSocket->Close ();
 }

//----------------------------------------------------------------------
//-- TimestampTag
//------------------------------------------------------
TypeId 
TimestampTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("TimestampTag")
    .SetParent<Tag> ()
    .AddConstructor<TimestampTag> ()
    .AddAttribute ("Timestamp",
                   "Some momentous point in time!",
                   EmptyAttributeValue (),
                   MakeTimeAccessor (&TimestampTag::GetTimestamp),
                   MakeTimeChecker ())
  ;
  return tid;
}
TypeId 
TimestampTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t 
TimestampTag::GetSerializedSize (void) const
{
  return 8;
}
void 
TimestampTag::Serialize (TagBuffer i) const
{
  int64_t t = m_timestamp.GetNanoSeconds ();
  i.Write ((const uint8_t *)&t, 8);
}
void 
TimestampTag::Deserialize (TagBuffer i)
{
  int64_t t;
  i.Read ((uint8_t *)&t, 8);
  m_timestamp = NanoSeconds (t);
}

void
TimestampTag::SetTimestamp (Time time)
{
  m_timestamp = time;
}
Time
TimestampTag::GetTimestamp (void) const
{
  return m_timestamp;
}

void 
TimestampTag::Print (std::ostream &os) const
{
  os << "t=" << m_timestamp;
}

bool TimestampTag::Equal(TimestampTag& t)const
{
  int x=m_timestamp.Compare(t.GetTimestamp());
  return x==0;
}
