#ifdef P4_CONCURY
#include "p4/hash.h"
#else

#include "hash.h"

#endif

#include "concury.common.h"
//#include <gperftools/profiler.h>

void printMemoryUsage() {
  uint64_t size = 0;
  
  for (int vipInd = 0; vipInd < VIP_NUM; ++vipInd)
    size += othelloForQuery[vipInd].getMemoryCost();
  
  cout << "Othello memory usage: " << size << endl;
  
  // dipTable
  size += VIP_NUM * HT_SIZE * sizeof(uint16_t) + DIP_NUM * sizeof(DIP);
  
  cout << "Total memory usage: " << size << endl;
  memoryLog << CONN_NUM << " " << size << endl;
}

void configureDataPlane(int vipInd) {
}

void updateDataPlaneCallBack(int vipInd) {
  // Step3: write back the new ht and new othelloForQuery
  memcpy(ht[vipInd], newHt[vipInd], HT_SIZE * sizeof(uint16_t));
  othelloForQuery[vipInd].fullSync(conn[vipInd]);
}

/**
 * read connection info from stdin, and log important cases:
 * hash conflicts of different conn: associate mem occupy (size and taken-up),
 * served 1M packets: time and average speed
 *
 * format: 1 byte: ip veprintMemoryUsagersion (server(dst)/client(src): 0x44, 0x46, 0x64, 0x66),
 * 1 byte Protocol Number (tcp 6/udp 17),
 * 4/16 byte src addr, 2 byte src port,
 * 4/16 byte dst addr, 2 byte dst port.
 */
void *serve(int *coreId) {
  const int id = *coreId;
  
  int addr = 0x0a800000 + id * 10;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, id * 10);
  
  stick_this_thread_to_core(id);
  
  struct timeval start, curr, last;
  gettimeofday(&start, NULL);
  last = start;
  int i = 0, round = 0;
  bool found = false;
  int stupid = 0;
  
  while (round < 5) {
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    
    tuple3Gen.gen(&tuple);
    vip.addr = addr++;
    vip.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = vip.addr & VIP_MASK;
    
    // Step 3: lookup corresponding Othello array
    uint16_t htInd = othelloForQuery[vipInd].query(tuple);
    htInd &= (HT_SIZE - 1);
    DIP &dip = dipPools[vipInd][ht[vipInd][htInd]];
    
    assert(((dip.addr.addr ^ (0x0a000000 + (vipInd << 8))) < dipPools[vipInd].size() && dip.addr.port - vip.port >= 0 &&
            dip.addr.port - vip.port < dipPools[vipInd].size()));
    
    stupid += dip.addr.addr;   //prevent optimize
    
    i++;
    if (i == LOG_INTERVAL) {
      i = 0;
      round++;
      // gettimeofday(&curr, NULL);
      // int diff = diff_ms(curr, start);
      // int mp = round * (LOG_INTERVAL / 1.0E6);
      
      // sync_printf("Core[%d] %dM packets time: %dms, Average speed: %lfMpps, %lfnspp\n", id, mp, diff, mp * 1000.0 / diff, diff * 1.0 / mp);
      
      // last = curr;
    }
  }
  
  sync_printf("%d\b \b", stupid & 7);
  pthread_exit(NULL);
}

#include <pthread.h>

/**
 * serving with multi-cores
 */
void multiThreadServe(int NUM_THREADS = 6) {
  pthread_t threads[8];
  char *b;
  int rc;
  int t[] = {0, 1, 2, 3, 4, 5, 6, 7};
  
  struct timeval start, curr, last;
  gettimeofday(&start, NULL);
  last = start;
  
  for (int i = 0; i < NUM_THREADS; i++) {
    rc = pthread_create(threads + i, NULL, (void *(*)(void *)) serve, t + i);
    if (rc) {
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
  }
  
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], (void **) &b);
  }
  
  gettimeofday(&curr, NULL);
  int diff = diff_ms(curr, start);
  
  queryLog << NUM_THREADS << ' ' << CONN_NUM << ' ' << 5 * LOG_INTERVAL / (diff / 1000.0) << endl;
}

void initControlPlaneAndDataPlane() {
  if (ht) {
    for (int i = 0; i < VIP_NUM; ++i) {
      delete[] ht[i];
      delete[] newHt[i];
    }
    
    delete ht;
    delete newHt;
  }
  
  ht = new uint16_t *[VIP_NUM];
  newHt = new uint16_t *[VIP_NUM];
  
  for (int i = 0; i < VIP_NUM; ++i) {
    ht[i] = new uint16_t[HT_SIZE];
    newHt[i] = new uint16_t[HT_SIZE];
    memset(ht[i], 0, sizeof(uint16_t[HT_SIZE]));
    memset(newHt[i], 0, sizeof(uint16_t[HT_SIZE]));
  }
  
  othelloForQuery = vector<DataPlaneOthello<Tuple3, uint16_t, 12>>(VIP_NUM);
  conn = vector<ControlPlaneOthello<Tuple3, uint16_t, 12, 0, false, false, false>>(VIP_NUM);
  
  for (auto &o: conn) {
    o.setMinimalKeyCapacity(CONN_NUM / VIP_NUM);
  }
  
  initDipPool();
}

void init() {
  commonInit();
  srand(time(0));
  initControlPlaneAndDataPlane();
}

///**
// * check the randomness
// */
//void randomness() {
//  for (int i = 0; i < 100; ++i) {
//    ofstream conRandLog(string("concury.rand.") + to_string(i));
//    othelloForQuery[i].outputMappedValue(conRandLog);
//    conRandLog.close();
//
//    ofstream md5RandLog(string("md5.rand.") + to_string(i));
//    ofstream sha256RandLog(string("sha256.rand.") + to_string(i));
//
//    int base = rand();
//    for (int j = base; j < base + (1 << 22); j++) {
//      int v = MD5(j).toInt();
//      md5RandLog << v << endl;
//
//      uint64_t const x[] = {(uint64_t) j};
//      auto res = sha2::sha512(sha2::bit_sequence(x));
//      sha256RandLog << (*(uint32_t *) &res[0]) << endl;
//    }
//
//    sha256RandLog.close();
//    md5RandLog.close();
//  }
//}

void dynamicServe() {
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  struct timeval start, curr, last;
  gettimeofday(&start, NULL);
  last = start;
  int i = 0, round = 0;
  bool found = false;
  int stupid = 0;
  
  while (round < 5) {
    double r = double(rand()) / RAND_MAX;
    if (r <= 1.1 / (1.1 + 1E6)) { // DIP update
      int vipInd = rand() % VIP_NUM;
      uint16_t dipCpunt = dipPools[vipInd].size();
      int dipInd = rand() % dipCpunt;
      int newWeight = log2(1 + (rand() % 64));
      cout << vipInd << " " << dipInd << " " << dipPools[vipInd][dipInd].weight << " -> " << newWeight << endl;
      dipPools[vipInd][dipInd].weight = newWeight;   // 0-49
      updateDataPlane(); // update one dip weight
    } else {  // serve packet
      // Step 1: read 5-tuple of a packet
      Tuple3 tuple;
      Addr_Port vip;
      
      tuple3Gen.gen(&tuple);
      vip.addr = addr++;
      vip.port = 0;
      if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
      
      // Step 2: lookup the VIPTable to get VIPInd
      uint16_t vipInd = vip.addr & VIP_MASK;
      
      // Step 3: lookup corresponding Othello array
      uint16_t htInd = othelloForQuery[vipInd].query(tuple);
      htInd &= (HT_SIZE - 1);
      uint16_t dipInd = ht[vipInd][htInd];
      DIP &dip = dipPools[vipInd][dipInd];
      
      stupid += dip.addr.addr;   //prevent optimize
      
      // Step 4: add to control plane tracking table
      if (!conn[vipInd].isMember(tuple)) {   // insert to the dipIndexTable to simulate the control plane
        conn[vipInd].insert(make_pair(tuple, htInd));
        uint16_t out;
        assert(conn[vipInd].isMember(tuple) && conn[vipInd].query(tuple, out) && (out & (HT_SIZE - 1)) == htInd);
      }
      
      i++;
      if (i == LOG_INTERVAL) {
        i = 0;
        round++;
        gettimeofday(&curr, NULL);
        int diff = diff_ms(curr, start);
        int mp = round * (LOG_INTERVAL / 1.0E6);
        
        printf("Dynamic: %dM packets time: %dms, Average speed: %lfMpps, %lfnspp\n", mp, diff, mp * 1000.0 / diff,
               diff * 1.0 / mp);
        
        last = curr;
      }
    }
  }
  
  printf("%d\b \b", stupid & 7);
}

void dynamicThroughput() {
  for (int newConnPerSec = 1024; newConnPerSec <= 256 * 1024 && newConnPerSec <= CONN_NUM; newConnPerSec *= 4) {
    struct timeval start, curr, last;
    
    int addr = 0x0a800000;
    LFSRGen<Tuple3> tuple3Gen(0xe2211, 1024 * 1024 + newConnPerSec, 0);
    
    int stupid = 0;
    
    // Clean control plane and data plane
    initControlPlaneAndDataPlane();
    simulateConnectionAdd(1024 * 1024, 0);
    updateDataPlane(true);
    
    uint64_t round = 50;
    gettimeofday(&start, NULL);
    for (uint64_t i = 0; i < round * LOG_INTERVAL; ++i) {
      // Step 1: read 5-tuple of a packet
      Tuple3 tuple;
      Addr_Port vip;
      
      tuple3Gen.gen(&tuple);
      vip.addr = addr++;
      vip.port = 0;
      if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
      
      // Step 2: lookup the VIPTable to get VIPInd
      uint16_t vipInd = vip.addr & VIP_MASK;
      
      // Step 3: lookup corresponding Othello array
      uint16_t htInd = othelloForQuery[vipInd].query(tuple);
      htInd &= (HT_SIZE - 1);
      uint16_t dipInd = ht[vipInd][htInd];
      DIP &dip = dipPools[vipInd][dipInd];
      
      if (i % (1024 * 1024 + newConnPerSec) < 1024 * 1024) {
        uint16_t out;
        assert(conn[vipInd].query(tuple, out) && out == htInd);
      } else if (i < 1024 * 1024 + newConnPerSec) {
        assert(!conn[vipInd].isMember(tuple));
      }
      
      stupid += dip.addr.addr;   //prevent optimize
    }
    gettimeofday(&last, NULL);
    
    printf("%d\b \b", stupid & 7);
    
    dynamicLog << newConnPerSec << " " << LOG_INTERVAL * 1.0 * round / diff_ms(last, start) / 1000 << endl;
  }
  dynamicLog.close();
}

void controlPlaneToDataPlaneUpdate(bool stupid = false) {
  ofstream updateTimeLog(string(NAME ".update.data") + (stupid ? ".stupid" : ""));
  for (int conn = 1024 * 1024; conn <= CONN_NUM; conn *= 2) {
    struct timeval start, curr, last;
    
    // Clean control plane and data plane
    initControlPlaneAndDataPlane();
    simulateConnectionAdd(conn, 0);
    simulateUpdatePoolData();
  
    gettimeofday(&start, NULL);
    if (stupid) {
      updateDataPlaneStupid(true);
    } else {
      updateDataPlane(true);
    }
    gettimeofday(&last, NULL);
    
    updateTimeLog << double(conn) / VIP_NUM << " " << diff_us(last, start) / 1000.0 / VIP_NUM << endl;
  }
  updateTimeLog.close();
}

#include <dirent.h>

void realTraceDistribution() {
  vector<string> files = {"10000000_1675245202791893_8515757603400187904_n",
                          "10000000_315017922212085_5340542123177410560_n",
                          "10000000_1126364437446015_9003528496516956160_n",
                          "10000000_1824528241117055_9163836690551275520_n",
                          "10000000_1205089662884486_4400531437447544832_n",
                          "10000000_1841586622752610_7085222864617472000_n",
                          "10000000_999303383512627_9020671020960317440_n",
                          "10000000_2112068879019036_2059714475182784512_n",
                          "10000000_317769855254506_8152085306355482624_n",
                          "10000000_874849092646205_6396562465437515776_n",
                          "10000000_1131221593631705_3102062192782999552_n",
                          "10000000_1153730474696090_4573827393541636096_n",
                          "10000000_1845242222428687_4961643655967277056_n",
                          "10000000_1760263417555467_8283342844110831616_n",
                          "10000000_1748505545391570_478311528295038976_n",
                          "10000000_710199019137690_802013974919905280_n",
                          "10000000_1258287940869175_7157609341481123840_n",
                          "10000000_1797978517114731_491547681198440448_n",
                          "10000000_1774046189534986_5643490342935199744_n",
                          "10000000_179070485868911_7075363629750550528_n",
                          "10000000_187049201735537_3780633378777202688_n",
                          "10000000_355175388155348_6317005317334892544_n",
                          "10000000_1598190443808429_8228777891660300288_n",
                          "10000000_678309828996854_5964461246194384896_n",
                          "10000000_508052449399525_8150586586107478016_n",
                          "10000000_1159082254171177_415917524773765120_n",
                          "10000000_534355520093341_7344322620789096448_n",
                          "10000000_203234523444661_6842751362095120384_n",
                          "10000000_307429996305091_6874657140833779712_n",
                          "10000000_524592414417737_5047483130043170816_n",
                          "10000000_212042589215317_1476621812337999872_n",
                          "10000000_1131555313598467_8353247688870854656_n",
                          "10000000_1780019358939610_4406944991851577344_n",
                          "10000000_314279128942772_7739701753687310336_n",
                          "10000000_746548222149823_6388999809713307648_n",
                          "10000000_1060741794041185_5328835365178441728_n",
                          "10000000_1625999457709573_4615576421278941184_n",
                          "10000000_960173747428241_1736251112207417344_n",
                          "10000000_1036734099757766_2645296333568606208_n",
                          "10000000_1791955251063261_4387576652146671616_n",
                          "10000000_1767757513486500_7243604581135941632_n",
                          "10000000_213386979075049_59238864711057408_n",
                          "10000000_1671743229808085_3480275767328243712_n",
                          "10000000_1718488578476736_5431868490988388352_n",
                          "10000000_339414859744201_167000337197039616_n",
                          "10000000_1809785802596124_8050214965631516672_n",
                          "10000000_184714695299993_3873789106303533056_n",
                          "10000000_874957972641054_6093210707732463616_n",
                          "10000000_742256779261507_3886677530064715776_n",
                          "10000000_1748847982043767_5709637868700303360_n",
                          "10000000_345611362451985_4555093497815760896_n",
                          "10000000_294849414232615_5404748297954918400_n",
                          "10000000_1613355792298002_1903158587916550144_n",
                          "10000000_550984961758163_1884426307098378240_n",
                          "10000000_968040006640510_6543871528633630720_n",
                          "10000000_1660141617635257_1672255764489568256_n",
                          "10000000_531164263741040_3892132774185795584_n",
                          "10000000_1114973748557320_951891862350725120_n",
                          "14617643_1784635495140461_4570682060141756416_n",
                          "10000000_129664584164086_8866151888790224896_n",
                          "10000000_524433961088216_1467667685994135552_n",
                          "10000000_266127410454664_8016642846956191744_n",
                          "10000000_319449951752689_3300195601152475136_n",
                          "10000000_278332312566597_6019585506466070528_n",
                          "10000000_1804962226382521_5727967852816760832_n",
                          "10000000_1548821955144709_3777473335114334208_n",
                          "10000000_1222953157775723_1253235701370060800_n",
                          "10000000_1145171122199387_6120026155656413184_n",
                          "10000000_1856422027921701_345382505232203776_n",
                          "10000000_775780395897194_5651293731576348672_n",
                          "10000000_313211325712178_7528404755886899200_n",
                          "10000000_628993100595769_5266511597257359360_n",
                          "10000000_1187282747997483_2828168215601872896_n",
                          "10000000_502136283329305_4399648722883969024_n",
                          "10000000_1710522765935649_4815237141493710848_n",
                          "10000000_171031600012430_1176606441063055360_n",
                          "10000000_198981490525451_6490658752146964480_n",
                          "10000000_1051116395004931_1234327404202164224_n",
                          "10000000_680418635454326_8270441934879719424_n",
                          "10000000_309927252697418_4372260206063648768_n",
                          "10000000_316690778711605_3597602411155292160_n",
                          "10000000_1858376451059570_5204218972144140288_n",
                          "10000000_180192792424778_3639668635112308736_n",
                          "10000000_269907053409188_4410276782666678272_n",
                          "10000000_343875762615613_8237864311586816000_n",
                          "10000000_1135412129884594_5253788031520866304_n",
                          "10000000_201773673578497_7160814585379815424_n",
                          "10000000_697441143741147_4416529086818549760_n",
                          "10000000_543356745871674_105022704985309184_n",
                          "10000000_182185332189778_3598915215153954816_n",
                          "10000000_1771786686395126_7413961362260361216_n",
                          "10000000_1077154542379892_8800742921306374144_n",
                          "10000000_168001350320026_3919914963413499904_n",
                          "10000000_1081920265255042_118751357493575680_n",
                          "10000000_220627975019916_5597249363521830912_n",
                          "10000000_190081798085241_1478338777054183424_n",
                          "10000000_147986905661069_473989313726513152_n",
                          "10000000_1590524424586041_8058355393241284608_n",
                          "10000000_310877529274050_556295532956352512_n",
                          "10000000_201320166964049_4891528740061839360_n",
                          "10000000_315829812118910_7323133524188332032_n",
                          "10000000_1155505047862000_830286026642554880_n",
                          "10000000_677833785706897_2069219508391772160_n",
                          "10000000_166965697088062_665438391478779904_n",
                          "10000000_1335779299795908_7691730714202472448_n",
                          "10000000_1505923789421638_9191910121511321600_n",
                          "10000000_315846125444776_1579280352575225856_n",
                          "10000000_1315209071846742_614222302338351104_n",
                          "10000000_1142588245833386_2091626773681799168_n",
                          "10000000_1184436554951550_2339952192074547200_n",
                          "10000000_1666769796969180_483724471283220480_n",
                          "10000000_1796611303950181_1507620468213940224_n",
                          "10000000_1168208386603411_1059882943788351488_n",
                          "10000000_548328045356823_6983019440589766656_n",
                          "10000000_521663704699296_65830183516504064_n",
                          "10000000_199230287172428_6748052126401822720_n",
                          "10000000_1820021794909637_7286345291052089344_n",
                          "10000000_1444668282214214_416408534024978432_n",
                          "10000000_680387655453202_8136909073774804992_n",
                          "10000000_548275975358081_2973436327518797824_n",
                          "10000000_1186791651379848_9091336178010947584_n",
                          "10000000_941687602642071_7603210802576752640_n",
                          "10000000_1760388514211341_8058176593752752128_n",
                          "10000000_891981430933523_3982776774772654080_n",
                          "10000000_1101472646573941_876596064835076096_n",
                          "10000000_1750251458549477_456153302388703232_n",
                          "10000000_174065113038411_8187261749934686208_n",
                          "10000000_271349786592291_9115125997568524288_n",
                          "10000000_1024997837611340_4052115925095153664_n",
                          "10000000_556197524572656_7974214767709519872_n",
                          "10000000_265533280507399_2969103349762228224_n",
                          "10000000_1201769326548404_1335200621023723520_n",
                          "10000000_966944396748741_6632159121183342592_n",
                          "10000000_1143630882382500_246491463062388736_n",
                          "10000000_1796702823937633_1821569069879394304_n",
                          "10000000_694307740708382_6880910291094208512_n",
                          "10000000_1743045982610920_1811188786759991296_n",
                          "10000000_568680446657598_3076514756643782656_n",
                          "10000000_1143552135715387_8015653445404983296_n",
                          "10000000_346146105727020_1668049466793394176_n",
                          "10000000_1244586752247796_7494640012223315968_n",
                          "10000000_125949021203041_7532206544973201408_n",
                          "10000000_197264850702649_3379323677727260672_n",
                          "10000000_311643299198437_2277716638486757376_n",
                          "10000000_709288262552875_6448873526425288704_n",
                          "10000000_1111136942304140_2945241421998718976_n",
                          "10000000_1228071533905079_4554973556559052800_n",
                          "10000000_1763529717222909_2570945600656769024_n",
                          "10000000_827926610682313_3338542873057427456_n",
                          "10000000_658629377633071_9017208594419941376_n",
                          "10000000_1126157740807788_9176497571240083456_n",
                          "10000000_1780052585574742_1126729153358331904_n",
                          "10000000_989886571120372_6549768153658818560_n",
                          "10000000_295547097496573_611013957473271808_n",
                          "10000000_1426004984084107_2135017629977411584_n",
                          "10000000_1715470935443971_2946659615904890880_n",
                          "10000000_1683469628637720_1090616196455202816_n",
                          "10000000_870476573054353_1385948691872874496_n",
                          "10000000_102085486928524_1722343428282384384_n",
                          "10000000_174335449678853_2972442497856307200_n",
                          "10000000_1744414305823009_8137360912924278784_n",
                          "10000000_1648231092134625_6724764259672129536_n",
                          "10000000_1790779997845737_2692156106100178944_n",
                          "10000000_985630901582597_8569964587506466816_n",
                          "10000000_1162693237099346_3458915967252299776_n",
                          "10000000_1464042536944373_1678263169606221824_n",
                          "10000000_344642019214346_1644203606502539264_n",
                          "10000000_653778358116241_3179748173958610944_n",
                          "10000000_259427177787963_8206944081813700608_n",
                          "10000000_1362163560468027_2832456096201834496_n",
                          "10000000_541413376052709_1883284644661559296_n",
                          "10000000_566786266838852_7007402687188697088_n",
                          "10000000_350160131986663_3399420972166545408_n",
                          "10000000_195483457553312_4403038693326061568_n",
                          "10000000_1628311577467688_2017612946594594816_n",
                          "10000000_334822546867168_5922635110909214720_n",
                          "10000000_382818712105672_4714636028030222336_n",
                          "10000000_342419666105495_1430571241709764608_n",
                          "10000000_1038929352871690_5384801718213017600_n",
                          "10000000_200578877038351_379756343977836544_n",
                          "10000000_1790850881192364_6603761759494864896_n",
                          "10000000_1816324715277875_2672787933898997760_n",
                          "10000000_129846270812646_8801847853182877696_n",
                          "10000000_317386941953529_3208815932797353984_n",
                          "10000000_299305837122126_9015810668464439296_n",
                          "10000000_1777731102440971_5229710612363214848_n",
                          "10000000_183559712052298_4336301150723637248_n",
                          "10000000_1696033614057058_6736609693575151616_n",
                          "10000000_663462687155449_3109852374399713280_n",
                          "10000000_109061086228826_5850819894760701952_n",
                          "10000000_1808899912685934_4987401356525436928_n",
                          "10000000_585561791637020_3162727514917306368_n",
                          "10000000_680159795474169_3024252391995211776_n",
                          "10000000_1107366899317220_5247577324127256576_n",
                          "10000000_1581421308831863_370434907735851008_n",
                          "10000000_1772802239628410_8677277549442629632_n",
                          "10000000_1511721508841885_7197022151822540800_n",
                          "10000000_336839476664872_4911134033112465408_n",
                          "10000000_195396127551376_5398700713419210752_n",
                          "10000000_160982387691347_3438488415597756416_n",
                          "10000000_1420377624643296_8233176633956106240_n",
                          "10000000_584373555084172_7981252144638459904_n",
                          "10000000_1078578722260623_6998206019746660352_n",
                          "10000000_1840726662829503_4255788677340332032_n",};
  FILE *dipDistributionLog = fopen(NAME ".facebook.dist.data", "w");
  uint32_t connOverDip[DIP_NUM];
  
  for (int j = 0; j < DIP_NUM; ++j)
    connOverDip[j] = 0;
  
  int i = 0;
  for (auto &name : files) {
    ifstream trace("/home/sshi27/Downloads/dataset.processed/" + name);
//    try {
    while (trace && !trace.eof()) {
      if (i >= 16 * 1024 * 1024) break;
      
      // Step 1: read 5-tuple of a packet
      Tuple3 tuple;
      Addr_Port vip;
      trace >> tuple.src.addr >> tuple.src.port >> vip.addr >> vip.port >> tuple.protocol;
      
      // Step 2: lookup the VIPTable to get VIPInd
      uint16_t vipInd = 0; // vip.addr & VIP_MASK;
      
      // Step 3: lookup corresponding Othello array
      uint16_t htInd;
      conn[vipInd].query(tuple, htInd);
      htInd &= (HT_SIZE - 1);
      uint16_t dipInd = ht[vipInd][htInd];
      DIP &dip = dipPools[vipInd][dipInd];
      
      // Step 4: add to control plane tracking table
      if (!conn[vipInd].isMember(tuple)) {   // insert to the dipIndexTable to simulate the control plane
        conn[vipInd].insert(make_pair(tuple, htInd));
        uint16_t out;
        assert(conn[vipInd].isMember(tuple) && conn[vipInd].query(tuple, out) && (out & (HT_SIZE - 1)) == htInd);
        
        fprintf(dipDistributionLog, "%u %u %u\n", (uint32_t) vipInd, (uint32_t) htInd, (uint32_t) dipInd);
        connOverDip[dipInd] += 1;
        i++;
      }
    }
//    } catch (exception &e) {
//      cerr << e.what() << endl;
//    }
    
    try {
      trace.close();
    } catch (exception &e) {
    }
  }
  
  fclose(dipDistributionLog);
}

void mockTraceDistribution() {
  uint32_t connOverDip[DIP_NUM];
  FILE *dipDistributionLog = fopen(NAME ".mock.dist.data", "w");
  
  int addr = 0x0a800000;
  LFSRGen<Tuple3> tuple3Gen(0xe2211, CONN_NUM, 0);
  
  for (int j = 0; j < DIP_NUM; ++j)
    connOverDip[j] = 0;
  
  int i = 0;
  while (i < 16 * 1024 * 1024) {
    // Step 1: read 5-tuple of a packet
    Tuple3 tuple;
    Addr_Port vip;
    
    tuple3Gen.gen(&tuple);
    vip.addr = addr++;
    vip.port = 0;
    if (addr >= 0x0a800000 + VIP_NUM) addr = 0x0a800000;
    
    // Step 2: lookup the VIPTable to get VIPInd
    uint16_t vipInd = 0; // vip.addr & VIP_MASK;
    
    // Step 3: lookup corresponding Othello array
    uint16_t htInd;
    conn[vipInd].query(tuple, htInd);
    htInd &= (HT_SIZE - 1);
    uint16_t dipInd = ht[vipInd][htInd];
    DIP &dip = dipPools[vipInd][dipInd];
    
    // Step 4: add to control plane tracking table
    if (!conn[vipInd].isMember(tuple)) {   // insert to the dipIndexTable to simulate the control plane
      conn[vipInd].insert(make_pair(tuple, htInd));
      uint16_t out;
      assert(conn[vipInd].isMember(tuple) && conn[vipInd].query(tuple, out) && (out & (HT_SIZE - 1)) == htInd);
      
      fprintf(dipDistributionLog, "%u %u %u\n", (uint32_t) vipInd, (uint32_t) htInd, (uint32_t) dipInd);
      connOverDip[dipInd] += 1;
      i++;
    }
  }
  
  fclose(dipDistributionLog);
}

int main(int argc, char **argv) {
  cout << "--init" << endl;
  init();

#ifdef DIST
  ofstream connHtDistLog("concury.ht.dist.data");
  for (int htInd = 0; htInd < HT_SIZE; ++htInd) {
    connHtDistLog << 0 << " " << ht[0][htInd] << " " << endl;
  }
  connHtDistLog.close();

  ofstream connDipWeightLog("concury.dip.weight.data");
  for (uint16_t j = 0; j < DIP_NUM; ++j) {
    connDipWeightLog << dipPools[0][j].weight << endl;
  }
  connDipWeightLog.close();

  cout << "--realTraceDistribution" << endl;
  realTraceDistribution();

  cout << "--mockTraceDistribution" << endl;
  mockTraceDistribution();
#else
  cout << "--simulateConnectionAdd" << endl;
  simulateConnectionAdd();
  cout << "--simulateUpdatePoolData" << endl;
  simulateUpdatePoolData();
  cout << "--updateDataPlane" << endl;
  updateDataPlane();
  cout << "--printMemoryUsage" << endl;
  printMemoryUsage();

//  cout << "--testResillience" << endl;
//  testResillience();
  
  multiThreadServe(1);
  multiThreadServe(2);
  multiThreadServe(4);
  multiThreadServe(8);
  
  if (CONN_NUM == 16777216) {
    cout << "--dynamicThroughput" << endl;
    dynamicThroughput();
    
    cout << "--controlPlaneToDataPlaneUpdate" << endl;
    controlPlaneToDataPlaneUpdate();
    
    cout << "--[stupid] controlPlaneToDataPlaneUpdate" << endl;
    controlPlaneToDataPlaneUpdate(true);
  }

//  if (VIP_NUM >= 100) {
//    cout << "--randomness" << endl;
//    randomness();
//  } else {
//    cout << "--randomness needs 100+ different othellos as samples" << endl;
//  }
  cout << "--dynamicServe" << endl;
  dynamicServe();
#endif
  return 0;
}
