// main/tianji_logic.c — 天机·六爻: 六爻起卦与六十四卦逻辑引擎
#include "tianji_logic.h"
#include <stdlib.h>
#include <string.h>

// 八卦自然象征
static const char *s_trigram_names[8] = {"坤", "震", "坎", "兑", "艮", "离", "巽", "乾"};
static const char *s_trigram_natures[8] = {"地", "雷", "水", "泽", "山", "火", "风", "天"};

// 64 卦对照表 (按 bits 0..63 线性排列, 纯只读进入 Flash .rodata, 0 字节 RAM 开销)
static const tianji_hexagram_t s_hexagram_by_bits[64] = {
    [0] = {
        .index = 2,
        .bits = 0,
        .name = "坤为地",
        .pinyin_name = "坤（kūn）为地",
        .short_name = "坤",
        .upper_name = "坤",
        .lower_name = "坤",
        .judgment = "元亨，利牝马之贞。君子有攸往，先迷后得主。",
        .xiang = "厚德载物，包容万物之象。",
        .career = "宜柔顺待时，配合辅佐，不宜冒进。",
        .wealth = "稳中求利，以静制动，渐入佳境。",
        .relationship = "温柔包容，相敬如宾，大吉之兆。",
    },
    [1] = {
        .index = 24,
        .bits = 1,
        .name = "地雷复",
        .pinyin_name = "地雷复（fù）",
        .short_name = "复",
        .upper_name = "坤",
        .lower_name = "震",
        .judgment = "亨。出入无疾，朋来无咎。七日来复。",
        .xiang = "一阳来复，万象更新之象。",
        .career = "冬去春来，转机已现，重整旗鼓。",
        .wealth = "资金回暖，生机萌发，渐见成效。",
        .relationship = "旧情重温，误会解除，复合有望。",
    },
    [2] = {
        .index = 7,
        .bits = 2,
        .name = "地水师",
        .pinyin_name = "地水师（shī）",
        .short_name = "师",
        .upper_name = "坤",
        .lower_name = "坎",
        .judgment = "贞，丈人吉，无咎。",
        .xiang = "行军用兵，纪律严明之象。",
        .career = "统帅有方，行事需有章法和纪律。",
        .wealth = "团队协作生财，严格风控。",
        .relationship = "需彼此信任，同甘共苦，方能长久。",
    },
    [3] = {
        .index = 19,
        .bits = 3,
        .name = "地泽临",
        .pinyin_name = "地泽临（lín）",
        .short_name = "临",
        .upper_name = "坤",
        .lower_name = "兑",
        .judgment = "元亨利贞。至于八月有凶。",
        .xiang = "以上临下，督导关怀之象。",
        .career = "居高临下，运势上升，宜抓紧行动。",
        .wealth = "渐入佳境，收益可观，见好就收。",
        .relationship = "主动关怀，感情迅速升温，吉。",
    },
    [4] = {
        .index = 15,
        .bits = 4,
        .name = "地山谦",
        .pinyin_name = "地山谦（qiān）",
        .short_name = "谦",
        .upper_name = "坤",
        .lower_name = "艮",
        .judgment = "亨，君子有终。谦谦君子，卑以自牧。",
        .xiang = "内高外卑，谦逊受益之象。",
        .career = "德才兼备，低调行事，必受尊崇。",
        .wealth = "财运平稳，不贪非分之财，久发其祥。",
        .relationship = "相敬如宾，彼此谦让，情比金坚。",
    },
    [5] = {
        .index = 36,
        .bits = 5,
        .name = "地火明夷",
        .pinyin_name = "地火明夷（yí）",
        .short_name = "明夷",
        .upper_name = "坤",
        .lower_name = "离",
        .judgment = "利艰贞。韬光养晦，避祸自保。",
        .xiang = "日落地下，韬光养晦之象。",
        .career = "遭遇阻力，低调收敛，静待黎明。",
        .wealth = "防财务暗亏，不宜外借，谨慎避险。",
        .relationship = "外界干扰多，彼此守护，共同熬过低谷。",
    },
    [6] = {
        .index = 46,
        .bits = 6,
        .name = "地风升",
        .pinyin_name = "地风升（shēng）",
        .short_name = "升",
        .upper_name = "坤",
        .lower_name = "巽",
        .judgment = "元亨，用见大人，勿恤，南征吉。",
        .xiang = "木生地下，蒸蒸日上之象。",
        .career = "积小成大，步步高升，前程光明。",
        .wealth = "收益稳步增长，投资循序推进。",
        .relationship = "感情稳固递增，水到渠成，终成良配。",
    },
    [7] = {
        .index = 11,
        .bits = 7,
        .name = "地天泰",
        .pinyin_name = "地天泰（tài）",
        .short_name = "泰",
        .upper_name = "坤",
        .lower_name = "乾",
        .judgment = "小往大来，吉亨。天地交而万物通。",
        .xiang = "天地交泰，三阳开泰之象。",
        .career = "诸事亨通，气运盛大，奋发有为。",
        .wealth = "财运大好，投策得当，获利丰厚。",
        .relationship = "心心相印，天作之合，喜事连连。",
    },
    [8] = {
        .index = 16,
        .bits = 8,
        .name = "雷地豫",
        .pinyin_name = "雷地豫（yù）",
        .short_name = "豫",
        .upper_name = "震",
        .lower_name = "坤",
        .judgment = "利建侯行师。先王以作乐崇德。",
        .xiang = "雷出地奋，欢愉欣喜之象。",
        .career = "顺应时势，心情愉悦，推进顺利。",
        .wealth = "收入看好，投资顺利，防乐极生悲。",
        .relationship = "相处欢快，浪漫有加，适宜推进婚嫁。",
    },
    [9] = {
        .index = 51,
        .bits = 9,
        .name = "震为雷",
        .pinyin_name = "震（zhèn）为雷",
        .short_name = "震",
        .upper_name = "震",
        .lower_name = "震",
        .judgment = "亨。震来虩虩，笑言哑哑。警醒奋发。",
        .xiang = "雷霆万钧，声威远播之象。",
        .career = "警惕自省，虽有惊险，终能化险为夷。",
        .wealth = "行情震荡，波段操作，保持警觉。",
        .relationship = "突生小风波，坦诚沟通可安好。",
    },
    [10] = {
        .index = 40,
        .bits = 10,
        .name = "雷水解",
        .pinyin_name = "雷水解（jiě）",
        .short_name = "解",
        .upper_name = "震",
        .lower_name = "坎",
        .judgment = "利西南；无所往，其来复吉。有攸往，夙吉。",
        .xiang = "雷雨大作，万物解冻之象。",
        .career = "危机消解，困境化除，雷厉风行。",
        .wealth = "债务脱钩，纠纷平息，财务回温。",
        .relationship = "冰释前嫌，重归于好，轻松愉快。",
    },
    [11] = {
        .index = 54,
        .bits = 11,
        .name = "雷泽归妹",
        .pinyin_name = "雷泽归妹（guī）",
        .short_name = "归妹",
        .upper_name = "震",
        .lower_name = "兑",
        .judgment = "征凶，无攸利。少女配长男，失衡有偏。",
        .xiang = "雷震泽动，冲动结合之象。",
        .career = "不合常规，易生偏差，重理智莫盲动。",
        .wealth = "防财务透支，投资谨慎，切忌跟风。",
        .relationship = "热情过盛易失平衡，需注重现实基础。",
    },
    [12] = {
        .index = 62,
        .bits = 12,
        .name = "雷山小过",
        .pinyin_name = "雷山小过（guò）",
        .short_name = "小过",
        .upper_name = "震",
        .lower_name = "艮",
        .judgment = "亨，利贞。可小事，不可大事。小有越轨。",
        .xiang = "山上有雷，微有过度之象。",
        .career = "宜办小事，不可行大计，低姿态为妙。",
        .wealth = "小账无忧，大额慎投，谨慎理财。",
        .relationship = "小有摩擦，相互包容，不宜激化。",
    },
    [13] = {
        .index = 55,
        .bits = 13,
        .name = "雷火丰",
        .pinyin_name = "雷火丰（fēng）",
        .short_name = "丰",
        .upper_name = "震",
        .lower_name = "离",
        .judgment = "亨，王假之，勿忧，宜日中。盛大光明。",
        .xiang = "雷电交加，盛大繁盛之象。",
        .career = "如日中天，业务达到顶峰，居安思危。",
        .wealth = "大发利市，利润丰厚，兼顾后续。",
        .relationship = "热烈浪漫，如胶似漆，珍惜缘分。",
    },
    [14] = {
        .index = 32,
        .bits = 14,
        .name = "雷风恒",
        .pinyin_name = "雷风恒（héng）",
        .short_name = "恒",
        .upper_name = "震",
        .lower_name = "巽",
        .judgment = "亨，无咎，利贞，利有攸往。",
        .xiang = "雷风相伴，持之以恒之象。",
        .career = "坚持不懈，循序渐进，守正有终。",
        .wealth = "长期稳定，细水长流，不可心浮。",
        .relationship = "相濡以沫，白头偕老，感情笃定。",
    },
    [15] = {
        .index = 34,
        .bits = 15,
        .name = "雷天大壮",
        .pinyin_name = "雷天大壮（zhuàng）",
        .short_name = "大壮",
        .upper_name = "震",
        .lower_name = "乾",
        .judgment = "利贞。刚健有力，声势壮大。",
        .xiang = "雷震于天，刚正威猛之象。",
        .career = "势头强劲，乘胜追击，但戒盲干。",
        .wealth = "财源大发，但防开支过大或盲目扩张。",
        .relationship = "热情主动，进展飞速，适度体贴。",
    },
    [16] = {
        .index = 8,
        .bits = 16,
        .name = "水地比",
        .pinyin_name = "水地比（bǐ）",
        .short_name = "比",
        .upper_name = "坎",
        .lower_name = "坤",
        .judgment = "吉。原筮，元永贞，无咎。不宁方来。",
        .xiang = "水地交融，亲和附丽之象。",
        .career = "得贵人相助，广结良缘，合作顺畅。",
        .wealth = "合伙生财，信誉为先，收益渐丰。",
        .relationship = "彼此倾心，亲密无间，吉庆祥和。",
    },
    [17] = {
        .index = 3,
        .bits = 17,
        .name = "水雷屯",
        .pinyin_name = "水雷屯（zhūn）",
        .short_name = "屯",
        .upper_name = "坎",
        .lower_name = "震",
        .judgment = "元亨利贞，勿用有攸往，利建侯。",
        .xiang = "万物始生，破土萌芽之象。",
        .career = "初创多难，宜积蓄实力，稳扎稳打。",
        .wealth = "初期见难，不宜急躁，蓄势后发。",
        .relationship = "波折初显，多加磨合，终成正果。",
    },
    [18] = {
        .index = 29,
        .bits = 18,
        .name = "坎为水",
        .pinyin_name = "坎（kǎn）为水",
        .short_name = "坎",
        .upper_name = "坎",
        .lower_name = "坎",
        .judgment = "习坎，有孚，维心亨，行有尚。",
        .xiang = "重重险阻，坚毅沉稳之象。",
        .career = "历经艰难，恪守诚信，沉着脱险。",
        .wealth = "财务遇险，步步为营，切忌赌徒心理。",
        .relationship = "关系起伏波折，患难方显真情。",
    },
    [19] = {
        .index = 60,
        .bits = 19,
        .name = "水泽节",
        .pinyin_name = "水泽节（jié）",
        .short_name = "节",
        .upper_name = "坎",
        .lower_name = "兑",
        .judgment = "亨。苦节不可贞。节制有度，适可而止。",
        .xiang = "泽中有水，适度节制之象。",
        .career = "制度规范，节制开销，过犹不及。",
        .wealth = "计划收支，精打细算，防超支透支。",
        .relationship = "相互留有余地，适度关怀，相敬如宾。",
    },
    [20] = {
        .index = 39,
        .bits = 20,
        .name = "水山蹇",
        .pinyin_name = "水山蹇（jiǎn）",
        .short_name = "蹇",
        .upper_name = "坎",
        .lower_name = "艮",
        .judgment = "利西南，不利东北；利见大人，贞吉。",
        .xiang = "高山流水，前路阻滞之象。",
        .career = "前进遇险，宜止不宜进，寻贵人相助。",
        .wealth = "资金周转难，节流为主，不宜投资。",
        .relationship = "阻碍较多，需长辈贵人撮合。",
    },
    [21] = {
        .index = 63,
        .bits = 21,
        .name = "水火既济",
        .pinyin_name = "水火既济（jì）",
        .short_name = "既济",
        .upper_name = "坎",
        .lower_name = "离",
        .judgment = "亨小，利贞。初吉终乱。大功告成。",
        .xiang = "水在火上，大功告成之象。",
        .career = "事情圆满达成，当防盛极转衰，守成为要。",
        .wealth = "收益兑现，落袋为安，防守为主。",
        .relationship = "圆满结合，感情稳定，细心经营保新鲜。",
    },
    [22] = {
        .index = 48,
        .bits = 22,
        .name = "水风井",
        .pinyin_name = "水风井（jǐng）",
        .short_name = "井",
        .upper_name = "坎",
        .lower_name = "巽",
        .judgment = "改邑不改井，无丧无得。养民无穷。",
        .xiang = "木入水出，润物无声之象。",
        .career = "深挖专业，潜心积累，源远流长。",
        .wealth = "正业稳财，持续滋养，不可投机。",
        .relationship = "深情内敛，默默关怀，持久纯真。",
    },
    [23] = {
        .index = 5,
        .bits = 23,
        .name = "水天需",
        .pinyin_name = "水天需（xū）",
        .short_name = "需",
        .upper_name = "坎",
        .lower_name = "乾",
        .judgment = "有孚，光亨，贞吉。利涉大川。",
        .xiang = "云行于天，待时而动之象。",
        .career = "时机未至，宜耐心守候，厚积薄发。",
        .wealth = "暂时持平，静待良机，不可妄动。",
        .relationship = "缘分需等待，顺其自然，终有良缘。",
    },
    [24] = {
        .index = 45,
        .bits = 24,
        .name = "泽地萃",
        .pinyin_name = "泽地萃（cuì）",
        .short_name = "萃",
        .upper_name = "兑",
        .lower_name = "坤",
        .judgment = "亨。王假有庙，利见大人，亨，利贞。",
        .xiang = "泽聚于地，荟萃汇聚之象。",
        .career = "群贤聚首，平台广阔，声势浩大。",
        .wealth = "资源聚合，团队分红，利润倍增。",
        .relationship = "情深意浓，良缘缔结，吉星拱照。",
    },
    [25] = {
        .index = 17,
        .bits = 25,
        .name = "泽雷随",
        .pinyin_name = "泽雷随（suí）",
        .short_name = "随",
        .upper_name = "兑",
        .lower_name = "震",
        .judgment = "元亨利贞，无咎。顺应时变。",
        .xiang = "随时变通，和顺适宜之象。",
        .career = "顺应潮流，择善而从，灵活应对。",
        .wealth = "随行就市，顺势而为，收益稳定。",
        .relationship = "相互依随，感情和睦，顺其自然。",
    },
    [26] = {
        .index = 47,
        .bits = 26,
        .name = "泽水困",
        .pinyin_name = "泽水困（kùn）",
        .short_name = "困",
        .upper_name = "兑",
        .lower_name = "坎",
        .judgment = "亨，贞，大人吉，无咎，有言不信。",
        .xiang = "泽无水溢，困顿坚守之象。",
        .career = "身处逆境，百折不挠，勿图多言。",
        .wealth = "财务困顿，开源节流，坚韧度过。",
        .relationship = "面临现实压力，相互体恤，切莫抱怨。",
    },
    [27] = {
        .index = 58,
        .bits = 27,
        .name = "兑为泽",
        .pinyin_name = "兑（duì）为泽",
        .short_name = "兑",
        .upper_name = "兑",
        .lower_name = "兑",
        .judgment = "亨，利贞。两泽相连，朋友讲习。",
        .xiang = "两泽互润，喜悦沟通之象。",
        .career = "善于沟通，合作共赢，愉悦顺利。",
        .wealth = "言语生财，利于销售谈判，笑脸迎财。",
        .relationship = "甜言蜜语，相处融洽，浪漫甜蜜。",
    },
    [28] = {
        .index = 31,
        .bits = 28,
        .name = "泽山咸",
        .pinyin_name = "泽山咸（xián）",
        .short_name = "咸",
        .upper_name = "兑",
        .lower_name = "艮",
        .judgment = "亨，利贞，取女吉。感应相交。",
        .xiang = "山上有泽，心灵感应之象。",
        .career = "以诚感人，配合默契，诸事顺利。",
        .wealth = "商机敏锐，财随缘聚，诚信合作。",
        .relationship = "两情相悦，一见钟情，大吉良缘。",
    },
    [29] = {
        .index = 49,
        .bits = 29,
        .name = "泽火革",
        .pinyin_name = "泽火革（gé）",
        .short_name = "革",
        .upper_name = "兑",
        .lower_name = "离",
        .judgment = "已日乃孚，元亨利贞，悔亡。",
        .xiang = "水火相息，除旧布新之象。",
        .career = "彻底变革，打破僵局，顺应时势。",
        .wealth = "转变盈利模式，敢于创新，新财大开。",
        .relationship = "更新相处方式，打破沉闷，重获激情。",
    },
    [30] = {
        .index = 28,
        .bits = 30,
        .name = "泽风大过",
        .pinyin_name = "泽风大过（guò）",
        .short_name = "大过",
        .upper_name = "兑",
        .lower_name = "巽",
        .judgment = "栋桡，利有攸往，亨。重担超负。",
        .xiang = "泽灭木顶，非常之事之象。",
        .career = "压力巨大，当机立断，勇挑重担。",
        .wealth = "支出超支，资金紧张，需非常调配。",
        .relationship = "感情面对考验，携手方能共渡难关。",
    },
    [31] = {
        .index = 43,
        .bits = 31,
        .name = "泽天夬",
        .pinyin_name = "泽天夬（guài）",
        .short_name = "夬",
        .upper_name = "兑",
        .lower_name = "乾",
        .judgment = "扬于王庭，孚号有厉。决断除去。",
        .xiang = "泽上升天，决断除恶之象。",
        .career = "当断不断必受其乱，果敢裁决。",
        .wealth = "快速止损，当机了结，防风险拖延。",
        .relationship = "态度需明朗，不可拖泥带水。",
    },
    [32] = {
        .index = 23,
        .bits = 32,
        .name = "山地剥",
        .pinyin_name = "山地剥（bō）",
        .short_name = "剥",
        .upper_name = "艮",
        .lower_name = "坤",
        .judgment = "不利有攸往。顺而止之，观象也。",
        .xiang = "山崖崩剥，物极必反之象。",
        .career = "处于退势，隐忍防守，避开锋芒。",
        .wealth = "破耗防亏，收缩战线，捂紧钱袋。",
        .relationship = "摩擦见多，感情冷淡，宜给彼此空间。",
    },
    [33] = {
        .index = 27,
        .bits = 33,
        .name = "山雷颐",
        .pinyin_name = "山雷颐（yí）",
        .short_name = "颐",
        .upper_name = "艮",
        .lower_name = "震",
        .judgment = "贞吉。观颐，自求口实。节食慎言。",
        .xiang = "山下动雷，修养生息之象。",
        .career = "注重身心涵养，谨言慎行，稳步推进。",
        .wealth = "正道求财，开销有节，不可盲目借贷。",
        .relationship = "共同进餐，生活安宁，相互扶持。",
    },
    [34] = {
        .index = 4,
        .bits = 34,
        .name = "山水蒙",
        .pinyin_name = "山水蒙（méng）",
        .short_name = "蒙",
        .upper_name = "艮",
        .lower_name = "坎",
        .judgment = "亨。匪我求童蒙，童蒙求我；初筮告，再三渎。",
        .xiang = "启蒙启智，拨云见日之象。",
        .career = "宜请教贤达，虚心求知，不可盲目。",
        .wealth = "谨慎规划，不可投机，求稳为上。",
        .relationship = "需加深了解，诚意相待，循序渐进。",
    },
    [35] = {
        .index = 41,
        .bits = 35,
        .name = "山泽损",
        .pinyin_name = "山泽损（sǔn）",
        .short_name = "损",
        .upper_name = "艮",
        .lower_name = "兑",
        .judgment = "有孚，元吉，无咎，可贞。利有攸往。",
        .xiang = "损下益上，克己助人之象。",
        .career = "短期吃亏，长期受益，重塑信誉。",
        .wealth = "先舍后得，投资未来，不可贪小利。",
        .relationship = "多付出关爱，体贴对方，必有回甘。",
    },
    [36] = {
        .index = 52,
        .bits = 36,
        .name = "艮为山",
        .pinyin_name = "艮（gèn）为山",
        .short_name = "艮",
        .upper_name = "艮",
        .lower_name = "艮",
        .judgment = "艮其背，不获其身；行其庭，不见其人。",
        .xiang = "重山叠嶂，安止不动之象。",
        .career = "知止不殆，按捺冲动，保持定力。",
        .wealth = "守财为上，不宜大动，稳住基本盘。",
        .relationship = "情绪冷静，各自独立，互不强求。",
    },
    [37] = {
        .index = 22,
        .bits = 37,
        .name = "山火贲",
        .pinyin_name = "山火贲（bì）",
        .short_name = "贲",
        .upper_name = "艮",
        .lower_name = "离",
        .judgment = "亨。小利有攸往。质文相称。",
        .xiang = "山下有火，文饰装点之象。",
        .career = "修饰外在，内实并重，利于面试宣传。",
        .wealth = "表面风光，实利需守，忌铺张浪费。",
        .relationship = "注重情调，相貌相吸，内质更需互赏。",
    },
    [38] = {
        .index = 18,
        .bits = 38,
        .name = "山风蛊",
        .pinyin_name = "山风蛊（gǔ）",
        .short_name = "蛊",
        .upper_name = "艮",
        .lower_name = "巽",
        .judgment = "元亨，利涉大川。先甲三日，后甲三日。",
        .xiang = "积弊待除，整饬革新之象。",
        .career = "直面旧弊，大刀阔斧，力挽狂澜。",
        .wealth = "清理坏账，整顿财务，重开新局。",
        .relationship = "化解误会，革新相处模式，破镜重圆。",
    },
    [39] = {
        .index = 26,
        .bits = 39,
        .name = "山天大畜",
        .pinyin_name = "山天大畜（xù）",
        .short_name = "大畜",
        .upper_name = "艮",
        .lower_name = "乾",
        .judgment = "利贞，不家食吉，利涉大川。",
        .xiang = "山藏广天，蕴蓄深厚之象。",
        .career = "蓄养实力，厚德大才，远行利就。",
        .wealth = "积少成多，储蓄充盈，投资长线。",
        .relationship = "感情稳固，彼此珍惜，水到渠成。",
    },
    [40] = {
        .index = 35,
        .bits = 40,
        .name = "火地晋",
        .pinyin_name = "火地晋（jìn）",
        .short_name = "晋",
        .upper_name = "离",
        .lower_name = "坤",
        .judgment = "康侯用锡马蕃庶，昼日三接。",
        .xiang = "日出地上，光明渐进之象。",
        .career = "步步高升，备受器重，顺风顺水。",
        .wealth = "薪酬晋升，业务扩展，财源渐隆。",
        .relationship = "感情发展顺畅，互敬互爱，前程锦绣。",
    },
    [41] = {
        .index = 21,
        .bits = 41,
        .name = "火雷噬嗑",
        .pinyin_name = "火雷噬嗑（shì kè）",
        .short_name = "噬嗑",
        .upper_name = "离",
        .lower_name = "震",
        .judgment = "亨。利用狱。除弊去奸，赏罚分明。",
        .xiang = "咬合决断，除弊立法之象。",
        .career = "排除阻碍，雷厉风行，决断立威。",
        .wealth = "破除阻隔，收回旧债，财路复通。",
        .relationship = "解开心结，直面问题，矛盾冰释。",
    },
    [42] = {
        .index = 64,
        .bits = 42,
        .name = "火水未济",
        .pinyin_name = "火水未济（jì）",
        .short_name = "未济",
        .upper_name = "离",
        .lower_name = "坎",
        .judgment = "亨。小狐汔济，濡其尾，无攸利。新起点。",
        .xiang = "火在水上，事未完结之象。",
        .career = "黎明在望，充满希望，蓄力冲刺。",
        .wealth = "新财路将开，重在布局，未来可期。",
        .relationship = "彼此情愫暗生，蓄势待发，大有希望。",
    },
    [43] = {
        .index = 38,
        .bits = 43,
        .name = "火泽睽",
        .pinyin_name = "火泽睽（kuí）",
        .short_name = "睽",
        .upper_name = "离",
        .lower_name = "兑",
        .judgment = "小事吉。求同存异，同床异梦。",
        .xiang = "火炎泽下，背道相驰之象。",
        .career = "分歧明显，宜求大同存小异，暂缓大事。",
        .wealth = "合伙生隙，账目分明，免伤和气。",
        .relationship = "性格观念有差异，需耐心沟通包容。",
    },
    [44] = {
        .index = 56,
        .bits = 44,
        .name = "火山旅",
        .pinyin_name = "火山旅（lǚ）",
        .short_name = "旅",
        .upper_name = "离",
        .lower_name = "艮",
        .judgment = "小亨，旅贞吉。羁旅飘零，随遇而安。",
        .xiang = "火在山上，羁流行走之象。",
        .career = "宜灵活应对，遵守当地规则，谨慎防身。",
        .wealth = "外出得财，利于出差外贸，防开支大。",
        .relationship = "聚少离多，多打电话多体贴，互报平安。",
    },
    [45] = {
        .index = 30,
        .bits = 45,
        .name = "离为火",
        .pinyin_name = "离（lí）为火",
        .short_name = "离",
        .upper_name = "离",
        .lower_name = "离",
        .judgment = "利贞，亨。畜牝牛，吉。明察照临。",
        .xiang = "两火相继，文明依附之象。",
        .career = "声名鹊起，热情进取，依附正道。",
        .wealth = "财气亨通，利于文娱名气，防浮夸。",
        .relationship = "热情似火，坦荡相爱，彼此依傍。",
    },
    [46] = {
        .index = 50,
        .bits = 46,
        .name = "火风鼎",
        .pinyin_name = "火风鼎（dǐng）",
        .short_name = "鼎",
        .upper_name = "离",
        .lower_name = "巽",
        .judgment = "元吉，亨。定鼎安邦，化生为熟。",
        .xiang = "木助火盛，去故取新之象。",
        .career = "稳固立威，权威确立，前程似锦。",
        .wealth = "基业稳固，财利双收，高端合作。",
        .relationship = "恩爱有加，相伴相成，家庭幸福。",
    },
    [47] = {
        .index = 14,
        .bits = 47,
        .name = "火天大有",
        .pinyin_name = "火天大有",
        .short_name = "大有",
        .upper_name = "离",
        .lower_name = "乾",
        .judgment = "元亨。顺天依时，大获丰收。",
        .xiang = "如日中天，丰盛富足之象。",
        .career = "运势正旺，收获满满，但忌骄矜。",
        .wealth = "财源滚滚，适时回馈，基业长青。",
        .relationship = "情意深浓，彼此欣赏，美满良缘。",
    },
    [48] = {
        .index = 20,
        .bits = 48,
        .name = "风地观",
        .pinyin_name = "风地观（guān）",
        .short_name = "观",
        .upper_name = "巽",
        .lower_name = "坤",
        .judgment = "盥而不荐，有孚颙若。观瞻省察。",
        .xiang = "风行地上，静心观察之象。",
        .career = "沉静反思，审时度势，谋定后动。",
        .wealth = "多观少动，谨慎评估，不宜急投。",
        .relationship = "观察对方心意，切忌盲目表白。",
    },
    [49] = {
        .index = 42,
        .bits = 49,
        .name = "风雷益",
        .pinyin_name = "风雷益（yì）",
        .short_name = "益",
        .upper_name = "巽",
        .lower_name = "震",
        .judgment = "利有攸往，利涉大川。损上益下。",
        .xiang = "雷风相得，增益丰厚之象。",
        .career = "机遇绝佳，进取有成，大展拳脚。",
        .wealth = "收益翻倍，良机涌现，获利丰厚。",
        .relationship = "彼此滋养，感情日深，大吉大利。",
    },
    [50] = {
        .index = 59,
        .bits = 50,
        .name = "风水涣",
        .pinyin_name = "风水涣（huàn）",
        .short_name = "涣",
        .upper_name = "巽",
        .lower_name = "坎",
        .judgment = "亨。王假有庙，利涉大川，利贞。冰消涣散。",
        .xiang = "风行水上，离散消释之象。",
        .career = "驱散阴霾，人心凝聚，化解分歧。",
        .wealth = "化解债务危机，财源重聚，逐步转暖。",
        .relationship = "消除猜忌与隔阂，坦诚挽回真情。",
    },
    [51] = {
        .index = 61,
        .bits = 51,
        .name = "风泽中孚",
        .pinyin_name = "风泽中孚（fú）",
        .short_name = "中孚",
        .upper_name = "巽",
        .lower_name = "兑",
        .judgment = "豚鱼吉，利涉大川，利贞。至诚感神。",
        .xiang = "风在泽上，诚信立身之象。",
        .career = "以诚待人，赢得信任，无往不利。",
        .wealth = "童叟无欺，信誉聚财，财源长久。",
        .relationship = "心心相印，真情实意，大吉良配。",
    },
    [52] = {
        .index = 53,
        .bits = 52,
        .name = "风山渐",
        .pinyin_name = "风山渐（jiàn）",
        .short_name = "渐",
        .upper_name = "巽",
        .lower_name = "艮",
        .judgment = "女归吉，利贞。循序渐进，积厚成高。",
        .xiang = "木植山上，循序渐进之象。",
        .career = "按部就班，稳扎稳打，自成大器。",
        .wealth = "稳步收益，不求暴利，细水长流。",
        .relationship = "传统良缘，明媒正娶，幸福美满。",
    },
    [53] = {
        .index = 37,
        .bits = 53,
        .name = "风火家人",
        .pinyin_name = "风火家人",
        .short_name = "家人",
        .upper_name = "巽",
        .lower_name = "离",
        .judgment = "利女贞。正家而天下定矣。",
        .xiang = "风自火出，齐家治生之象。",
        .career = "内部和睦，分工明确，后方稳固。",
        .wealth = "家庭兴旺，勤俭持家，家和万事兴。",
        .relationship = "温馨和睦，琴瑟和谐，适合组建家庭。",
    },
    [54] = {
        .index = 57,
        .bits = 54,
        .name = "巽为风",
        .pinyin_name = "巽（xùn）为风",
        .short_name = "巽",
        .upper_name = "巽",
        .lower_name = "巽",
        .judgment = "小亨，利有攸往，利见大人。和顺谦逊。",
        .xiang = "风行相随，无孔不入之象。",
        .career = "顺应环境，谦逊务实，渗透有力。",
        .wealth = "财路通达，灵活经营，多方获利。",
        .relationship = "温柔和顺，互体互谅，其乐融融。",
    },
    [55] = {
        .index = 9,
        .bits = 55,
        .name = "风天小畜",
        .pinyin_name = "风天小畜（xù）",
        .short_name = "小畜",
        .upper_name = "巽",
        .lower_name = "乾",
        .judgment = "亨。密云不雨，自我西郊。",
        .xiang = "微雨将至，蓄积未满之象。",
        .career = "小有成效，尚需积累，不宜操之过急。",
        .wealth = "小财可求，大财需候，节俭持家。",
        .relationship = "小有波折，多沟通可解，珍惜当下。",
    },
    [56] = {
        .index = 12,
        .bits = 56,
        .name = "天地否",
        .pinyin_name = "天地否（pǐ）",
        .short_name = "否",
        .upper_name = "乾",
        .lower_name = "坤",
        .judgment = "否之匪人，不利君子贞，大往小来。",
        .xiang = "闭塞不通，上下不交之象。",
        .career = "阻滞在先，守正防晦，隐忍待机。",
        .wealth = "开销吃紧，宜守财戒投，静观其变。",
        .relationship = "沟通不畅，各有心事，宜多包容。",
    },
    [57] = {
        .index = 25,
        .bits = 57,
        .name = "天雷无妄",
        .pinyin_name = "天雷无妄（wàng）",
        .short_name = "无妄",
        .upper_name = "乾",
        .lower_name = "震",
        .judgment = "元亨利贞。其匪正有眚，不利有攸往。",
        .xiang = "雷行天下，真实纯正之象。",
        .career = "守本分行事，顺理成章，勿生贪念。",
        .wealth = "正财可得，偏财莫取，防意外之失。",
        .relationship = "真诚相待，毫无欺瞒，感情纯粹。",
    },
    [58] = {
        .index = 6,
        .bits = 58,
        .name = "天水讼",
        .pinyin_name = "天水讼（sòng）",
        .short_name = "讼",
        .upper_name = "乾",
        .lower_name = "坎",
        .judgment = "有孚，窒惕，中吉，终凶。利见大人。",
        .xiang = "争端分歧，息事宁人之象。",
        .career = "避免争端，和睦为贵，宜退让求全。",
        .wealth = "防合同财务纠纷，账目需清。",
        .relationship = "彼此口角多，宜换位思考，切忌赌气。",
    },
    [59] = {
        .index = 10,
        .bits = 59,
        .name = "天泽履",
        .pinyin_name = "天泽履（lǚ）",
        .short_name = "履",
        .upper_name = "乾",
        .lower_name = "兑",
        .judgment = "履虎尾，不咥人，亨。",
        .xiang = "行步涉险，谨言慎行之象。",
        .career = "循礼而行，小心应对，虽险无妨。",
        .wealth = "按部就班，防风险，莫走偏门。",
        .relationship = "相互尊重，恪守本分，平稳发展。",
    },
    [60] = {
        .index = 33,
        .bits = 60,
        .name = "天山遁",
        .pinyin_name = "天山遁（dùn）",
        .short_name = "遁",
        .upper_name = "乾",
        .lower_name = "艮",
        .judgment = "亨，小利贞。见机而退，远害全身。",
        .xiang = "天退山远，避世自全之象。",
        .career = "退避保身，暂避锋芒，以退为进。",
        .wealth = "见好就收，抽回资金，避免套牢。",
        .relationship = "保持距离，冷静思考，切忌纠缠。",
    },
    [61] = {
        .index = 13,
        .bits = 61,
        .name = "天火同人",
        .pinyin_name = "天火同人",
        .short_name = "同人",
        .upper_name = "乾",
        .lower_name = "离",
        .judgment = "同人于野，亨。利涉大川，利君子贞。",
        .xiang = "志同道合，团结一致之象。",
        .career = "同心协力，众志成城，大展宏图。",
        .wealth = "合伙得利，公开透明，财源广进。",
        .relationship = "志趣相投，琴瑟和鸣，吉星高照。",
    },
    [62] = {
        .index = 44,
        .bits = 62,
        .name = "天风姤",
        .pinyin_name = "天风姤（gòu）",
        .short_name = "姤",
        .upper_name = "乾",
        .lower_name = "巽",
        .judgment = "女壮，勿用取女。偶遇相逢。",
        .xiang = "风行天下，不期而遇之象。",
        .career = "邂逅新缘，机会乍现，但防昙花一现。",
        .wealth = "偏财偶得，莫沉迷，防意外开销。",
        .relationship = "萍水相逢，热情易逝，细水方长。",
    },
    [63] = {
        .index = 1,
        .bits = 63,
        .name = "乾为天",
        .pinyin_name = "乾（qián）为天",
        .short_name = "乾",
        .upper_name = "乾",
        .lower_name = "乾",
        .judgment = "元亨利贞。天行健，自强不息。",
        .xiang = "刚健中正，自强不息之象。",
        .career = "大吉大利，名利双收，但戒骄忌躁。",
        .wealth = "财源亨通，宜守正求进。",
        .relationship = "阳刚有力，彼此契合，宜坦诚相待。",
    },
};

// 按周易序号 1..64 索引映射表
static const uint8_t s_index_to_bits[65] = {
    0,
    63, // 1: 乾为天
    0, // 2: 坤为地
    17, // 3: 水雷屯
    34, // 4: 山水蒙
    23, // 5: 水天需
    58, // 6: 天水讼
    2, // 7: 地水师
    16, // 8: 水地比
    55, // 9: 风天小畜
    59, // 10: 天泽履
    7, // 11: 地天泰
    56, // 12: 天地否
    61, // 13: 天火同人
    47, // 14: 火天大有
    4, // 15: 地山谦
    8, // 16: 雷地豫
    25, // 17: 泽雷随
    38, // 18: 山风蛊
    3, // 19: 地泽临
    48, // 20: 风地观
    41, // 21: 火雷噬嗑
    37, // 22: 山火贲
    32, // 23: 山地剥
    1, // 24: 地雷复
    57, // 25: 天雷无妄
    39, // 26: 山天大畜
    33, // 27: 山雷颐
    30, // 28: 泽风大过
    18, // 29: 坎为水
    45, // 30: 离为火
    28, // 31: 泽山咸
    14, // 32: 雷风恒
    60, // 33: 天山遁
    15, // 34: 雷天大壮
    40, // 35: 火地晋
    5, // 36: 地火明夷
    53, // 37: 风火家人
    43, // 38: 火泽睽
    20, // 39: 水山蹇
    10, // 40: 雷水解
    35, // 41: 山泽损
    49, // 42: 风雷益
    31, // 43: 泽天夬
    62, // 44: 天风姤
    24, // 45: 泽地萃
    6, // 46: 地风升
    26, // 47: 泽水困
    22, // 48: 水风井
    29, // 49: 泽火革
    46, // 50: 火风鼎
    9, // 51: 震为雷
    36, // 52: 艮为山
    52, // 53: 风山渐
    11, // 54: 雷泽归妹
    13, // 55: 雷火丰
    44, // 56: 火山旅
    54, // 57: 巽为风
    27, // 58: 兑为泽
    50, // 59: 风水涣
    19, // 60: 水泽节
    51, // 61: 风泽中孚
    12, // 62: 雷山小过
    21, // 63: 水火既济
    42, // 64: 火水未济
};

const char *tianji_trigram_name(tianji_trigram_t tri) {
    if ((int)tri < 0 || tri > 7) return "?";
    return s_trigram_names[tri];
}

const char *tianji_trigram_nature(tianji_trigram_t tri) {
    if ((int)tri < 0 || tri > 7) return "?";
    return s_trigram_natures[tri];
}

bool tianji_line_is_yang(tianji_line_type_t type) {
    return (type == TIANJI_LINE_SHAOYANG || type == TIANJI_LINE_LAOYANG);
}

bool tianji_line_is_moving(tianji_line_type_t type) {
    return (type == TIANJI_LINE_LAOYIN || type == TIANJI_LINE_LAOYANG);
}

bool tianji_line_transformed_is_yang(tianji_line_type_t type) {
    if (type == TIANJI_LINE_LAOYIN) return true;  // 老阴变阳
    if (type == TIANJI_LINE_LAOYANG) return false; // 老阳变阴
    return tianji_line_is_yang(type);
}

const tianji_hexagram_t *tianji_get_hexagram(uint8_t bits) {
    return &s_hexagram_by_bits[bits & 0x3F];
}

const tianji_hexagram_t *tianji_get_hexagram_by_index(uint8_t index) {
    if (index < 1 || index > 64) index = 1;
    return &s_hexagram_by_bits[s_index_to_bits[index]];
}

void tianji_session_init(tianji_session_t *session) {
    if (!session) return;
    memset(session, 0, sizeof(*session));
}

static void tianji_session_update_results(tianji_session_t *session) {
    uint8_t prim = 0;
    uint8_t trans = 0;
    uint8_t mov_cnt = 0;
    for (int i = 0; i < session->current_line; i++) {
        if (tianji_line_is_yang(session->lines[i])) {
            prim |= (1 << i);
        }
        if (tianji_line_transformed_is_yang(session->lines[i])) {
            trans |= (1 << i);
        }
        if (tianji_line_is_moving(session->lines[i])) {
            session->moving_indices[mov_cnt++] = i;
        }
    }
    session->primary_bits = prim;
    session->transformed_bits = trans;
    session->moving_count = mov_cnt;
    if (session->current_line >= 6) {
        session->is_completed = true;
    }
}

bool tianji_session_inject_line(tianji_session_t *session, tianji_line_type_t line_type) {
    if (!session || session->current_line >= 6) return false;
    int idx = session->current_line;
    session->lines[idx] = line_type;
    session->toss_history[idx].sum_score = (uint8_t)line_type;
    session->toss_history[idx].type = line_type;
    if (line_type == TIANJI_LINE_LAOYIN) {
        session->toss_history[idx].coins[0] = 0;
        session->toss_history[idx].coins[1] = 0;
        session->toss_history[idx].coins[2] = 0;
    } else if (line_type == TIANJI_LINE_SHAOYANG) {
        session->toss_history[idx].coins[0] = 0;
        session->toss_history[idx].coins[1] = 1;
        session->toss_history[idx].coins[2] = 1;
    } else if (line_type == TIANJI_LINE_SHAOYIN) {
        session->toss_history[idx].coins[0] = 0;
        session->toss_history[idx].coins[1] = 0;
        session->toss_history[idx].coins[2] = 1;
    } else {
        session->toss_history[idx].coins[0] = 1;
        session->toss_history[idx].coins[1] = 1;
        session->toss_history[idx].coins[2] = 1;
    }
    session->current_line++;
    tianji_session_update_results(session);
    return true;
}

bool tianji_session_cast_step(tianji_session_t *session, uint32_t (*rand_func)(void), tianji_toss_result_t *out_toss) {
    if (!session || session->current_line >= 6) return false;
    uint8_t c[3];
    uint8_t heads = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t r = rand_func ? rand_func() : (uint32_t)rand();
        c[i] = (r & 1);
        if (c[i]) heads++;
    }
    tianji_line_type_t ltype;
    if (heads == 0) ltype = TIANJI_LINE_LAOYIN;
    else if (heads == 1) ltype = TIANJI_LINE_SHAOYANG;
    else if (heads == 2) ltype = TIANJI_LINE_SHAOYIN;
    else ltype = TIANJI_LINE_LAOYANG;

    int idx = session->current_line;
    session->lines[idx] = ltype;
    session->toss_history[idx].coins[0] = c[0];
    session->toss_history[idx].coins[1] = c[1];
    session->toss_history[idx].coins[2] = c[2];
    session->toss_history[idx].sum_score = (heads == 0) ? 6 : ((heads == 1) ? 7 : ((heads == 2) ? 8 : 9));
    session->toss_history[idx].type = ltype;
    if (out_toss) {
        *out_toss = session->toss_history[idx];
    }
    session->current_line++;
    tianji_session_update_results(session);
    return true;
}
