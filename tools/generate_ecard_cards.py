#!/usr/bin/env python3
"""Generate the full grade 1-6 eCard card decks.

Outputs:

* ``main/ecard_cards.c`` - poem and word tables, grouped by grade (text only)
Pronunciation clips were removed together with the offline TTS assets; speech
is now provided by the Xiaozhi server at runtime.

Run from the repository root:

    python3 tools/generate_ecard_cards.py
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CARDS_OUT = REPO_ROOT / "main" / "ecard_cards.c"
SPEECH_OUT = REPO_ROOT / "assets" / "speech" / "ecard_speech_word.c"

GRADE_COUNT = 6
WORD_RATE = 16000
EN_VOICE = "Samantha"
EN_RATE = 130
SILENCE_FLOOR = 400

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]

# (grade, front line, back line, source, one-sentence meaning)
POEMS = [
    (1, "鹅鹅鹅", "曲项向天歌", "咏鹅 · 骆宾王",
     "白鹅弯着脖子，朝着天空欢快地唱歌。"),
    (1, "床前明月光", "疑是地上霜", "静夜思 · 李白",
     "明亮的月光洒在床前，好像地上结了一层白霜。"),
    (1, "春眠不觉晓", "处处闻啼鸟", "春晓 · 孟浩然",
     "春天睡得香甜，不知不觉天亮了，到处都能听到鸟叫。"),
    (1, "锄禾日当午", "汗滴禾下土", "悯农 · 李绅",
     "农民正午在田里锄草，汗水一滴滴落进泥土里。"),
    (1, "远看山有色", "近听水无声", "画 · 王维",
     "远看山有颜色，走近却听不到流水的声音。"),
    (1, "一去二三里", "烟村四五家", "山村咏怀 · 邵雍",
     "一眼望去，小山村里散落着几户人家，炊烟袅袅。"),
    (1, "小荷才露尖尖角", "早有蜻蜓立上头", "小池 · 杨万里",
     "嫩嫩的荷叶刚露出水面，蜻蜓早已立在上面。"),
    (1, "李白乘舟将欲行", "忽闻岸上踏歌声", "赠汪伦 · 李白",
     "李白坐船刚要出发，忽然听到岸上传来送别的歌声。"),
    (1, "白日依山尽", "黄河入海流", "登鹳雀楼 · 王之涣",
     "太阳靠着群山慢慢落下，黄河奔腾着流向大海。"),
    (1, "千山鸟飞绝", "万径人踪灭", "江雪 · 柳宗元",
     "群山里鸟儿都不见了，小路上也看不到人影。"),

    (2, "野火烧不尽", "春风吹又生", "草 · 白居易",
     "野草被火烧不尽，春风一吹又长出来。"),
    (2, "不知细叶谁裁出", "二月春风似剪刀", "咏柳 · 贺知章",
     "不知细嫩的柳叶是谁裁出来的，原来是二月春风像剪刀。"),
    (2, "接天莲叶无穷碧", "映日荷花别样红", "晓出净慈寺送林子方 · 杨万里",
     "满池荷叶绿得望不到边，荷花在阳光下红得格外鲜艳。"),
    (2, "迟日江山丽", "春风花草香", "绝句 · 杜甫",
     "春天白昼变长，江山明丽，春风送来花草的香气。"),
    (2, "敕勒川阴山下", "天似穹庐笼盖四野", "敕勒歌 · 北朝民歌",
     "敕勒川在阴山脚下，天空像大帐篷罩住四面的原野。"),
    (2, "草长莺飞二月天", "拂堤杨柳醉春烟", "村居 · 高鼎",
     "二月里草长莺飞，杨柳拂着堤岸，像陶醉在春烟里。"),
    (2, "牧童骑黄牛", "歌声振林樾", "所见 · 袁枚",
     "牧童骑着黄牛，响亮的歌声震动了树林。"),
    (2, "远上寒山石径斜", "白云生处有人家", "山行 · 杜牧",
     "沿石路上寒山，在白云升起的地方住着人家。"),
    (2, "荷尽已无擎雨盖", "菊残犹有傲霜枝", "赠刘景文 · 苏轼",
     "荷花谢了，菊花虽残还留着傲霜的枝。"),
    (2, "移舟泊烟渚", "日暮客愁新", "宿建德江 · 孟浩然",
     "把船停在烟雾中的小洲边，天黑了旅人又添愁绪。"),

    (3, "天门中断楚江开", "碧水东流至此回", "望天门山 · 李白",
     "天门山被楚江冲开，碧绿的水到这里打了个回旋。"),
    (3, "水光潋滟晴方好", "山色空蒙雨亦奇", "饮湖上初晴后雨 · 苏轼",
     "晴天湖水波光好看，雨天山色朦胧也很奇妙。"),
    (3, "湖光秋月两相和", "潭面无风镜未磨", "望洞庭 · 刘禹锡",
     "湖光和秋月交融，没有风的湖面像一面未磨的镜子。"),
    (3, "朝辞白帝彩云间", "千里江陵一日还", "早发白帝城 · 李白",
     "清晨从彩云间的白帝城出发，一天就回到千里外的江陵。"),
    (3, "独在异乡为异客", "每逢佳节倍思亲", "九月九日忆山东兄弟 · 王维",
     "独自在他乡做客人，每到节日就更想念亲人。"),
    (3, "爆竹声中一岁除", "春风送暖入屠苏", "元日 · 王安石",
     "爆竹声里旧的一年过去，春风送暖，人们喝着屠苏酒。"),
    (3, "清明时节雨纷纷", "路上行人欲断魂", "清明 · 杜牧",
     "清明时节细雨不停，路上的行人心情低落。"),
    (3, "独怜幽草涧边生", "上有黄鹂深树鸣", "滁州西涧 · 韦应物",
     "偏爱山涧边生长的青草，树深处还有黄鹂在鸣叫。"),
    (3, "七夕今宵看碧霄", "牵牛织女渡河桥", "乞巧 · 林杰",
     "七夕的夜晚仰望碧空，牛郎织女在鹊桥上相会。"),
    (3, "云母屏风烛影深", "长河渐落晓星沉", "嫦娥 · 李商隐",
     "烛光映在屏风上，银河慢慢落下，晨星也沉下去了。"),

    (4, "横看成岭侧成峰", "远近高低各不同", "题西林壁 · 苏轼",
     "横着看是山岭，侧面看是山峰，远近高低看都不一样。"),
    (4, "山重水复疑无路", "柳暗花明又一村", "游山西村 · 陆游",
     "山水重重以为没有路了，转过去又看见一个花木繁盛的村子。"),
    (4, "故人西辞黄鹤楼", "烟花三月下扬州", "黄鹤楼送孟浩然之广陵 · 李白",
     "老朋友在黄鹤楼告别，在繁花的三月去往扬州。"),
    (4, "渭城朝雨浥轻尘", "客舍青青柳色新", "送元二使安西 · 王维",
     "渭城的晨雨润湿尘土，旅店旁的柳树格外青翠。"),
    (4, "众鸟高飞尽", "孤云独去闲", "独坐敬亭山 · 李白",
     "鸟儿都飞走了，一朵孤云也悠闲地飘远。"),
    (4, "日出江花红胜火", "春来江水绿如蓝", "忆江南 · 白居易",
     "太阳照着江边花比火还红，春天的江水绿得像蓝草。"),
    (4, "绿遍山原白满川", "子规声里雨如烟", "乡村四月 · 翁卷",
     "山野一片绿色，河面白亮，杜鹃声里细雨如烟。"),
    (4, "昼出耘田夜绩麻", "村庄儿女各当家", "四时田园杂兴 · 范成大",
     "白天锄地夜里搓麻线，村里的男女各有各的活。"),
    (4, "西塞山前白鹭飞", "桃花流水鳜鱼肥", "渔歌子 · 张志和",
     "西塞山前白鹭飞过，桃花流水中鳜鱼正肥。"),
    (4, "吾家洗砚池头树", "个个花开淡墨痕", "墨梅 · 王冕",
     "我家洗砚池边的梅树，开的花都带着淡淡的墨痕。"),

    (5, "草铺横野六七里", "笛弄晚风三四声", "牧童 · 吕岩",
     "青草铺满六七里的原野，晚风里传来三四声笛响。"),
    (5, "一叶渔船两小童", "收篙停棹坐船中", "舟过安仁 · 杨万里",
     "一只小渔船上有两个小孩，收桨停船坐在舱里。"),
    (5, "茅檐低小", "溪上青青草", "清平乐·村居 · 辛弃疾",
     "茅屋低矮，小溪边长满青草。"),
    (5, "京口瓜洲一水间", "钟山只隔数重山", "泊船瓜洲 · 王安石",
     "京口和瓜洲只隔一条江，钟山也只是几重山之外。"),
    (5, "洛阳城里见秋风", "欲作家书意万重", "秋思 · 张籍",
     "洛阳城里刮起秋风，想写家书却觉得千言万语。"),
    (5, "山一程水一程", "身向榆关那畔行", "长相思 · 纳兰性德",
     "翻过一山走过一水，队伍向着榆关那边行进。"),
    (5, "死去元知万事空", "但悲不见九州同", "示儿 · 陆游",
     "本知死后万事皆空，只悲叹没能看到国家统一。"),
    (5, "山外青山楼外楼", "西湖歌舞几时休", "题临安邸 · 林升",
     "青山外还有青山，楼外还有楼，西湖边的歌舞何时才停。"),
    (5, "九州生气恃风雷", "万马齐喑究可哀", "己亥杂诗 · 龚自珍",
     "国家要有生气得靠风雷般的变革，万马无声实在可悲。"),
    (5, "半亩方塘一鉴开", "天光云影共徘徊", "观书有感 · 朱熹",
     "半亩大的方塘像一面镜子，天光云影都在水里晃动。"),

    (6, "寒雨连江夜入吴", "平明送客楚山孤", "芙蓉楼送辛渐 · 王昌龄",
     "寒雨连江的夜里来到吴地，天亮送走客人只剩楚山孤影。"),
    (6, "黄四娘家花满蹊", "千朵万朵压枝低", "江畔独步寻花 · 杜甫",
     "黄四娘家门前花开满径，千朵万朵把枝头都压低了。"),
    (6, "千锤万凿出深山", "烈火焚烧若等闲", "石灰吟 · 于谦",
     "石灰历经千锤万凿出自深山，烈火焚烧也看得平常。"),
    (6, "咬定青山不放松", "立根原在破岩中", "竹石 · 郑燮",
     "竹子紧咬青山不放松，根扎在破裂的岩石中。"),
    (6, "白日放歌须纵酒", "青春作伴好还乡", "闻官军收河南河北 · 杜甫",
     "趁着白天放声高歌还要纵酒，有春光作伴正好回家乡。"),
    (6, "慈母手中线", "游子身上衣", "游子吟 · 孟郊",
     "慈母手里的针线，缝出游子身上的衣裳。"),
    (6, "才始送春归", "又送君归去", "卜算子·送鲍浩然之浙东 · 王观",
     "刚刚送走春天，又要送你归去。"),
    (6, "谁道人生无再少", "门前流水尚能西", "浣溪沙 · 苏轼",
     "谁说人生不能再年轻，门前的流水还能向西流呢。"),
    (6, "好雨知时节", "当春乃发生", "春夜喜雨 · 杜甫",
     "好雨懂得节气，正当春天就下起来。"),
    (6, "煮豆燃豆萁", "豆在釜中泣", "七步诗 · 曹植",
     "煮豆子烧的是豆秸，豆子在锅里哭泣。"),
]

# (grade, word, Chinese, example sentence, translation)
WORDS = [
    # Grade 1
    (1, "hello", "你好", "Hello, my name is Tom.", "你好，我叫汤姆。"),
    (1, "hi", "嗨", "Hi, I am Amy.", "嗨，我是艾米。"),
    (1, "bye", "再见", "Bye, see you tomorrow.", "再见，明天见。"),
    (1, "name", "名字", "My name is Mike.", "我的名字叫迈克。"),
    (1, "boy", "男孩", "He is a happy boy.", "他是个快乐的男孩。"),
    (1, "girl", "女孩", "She is a little girl.", "她是个小女孩。"),
    (1, "friend", "朋友", "You are my friend.", "你是我的朋友。"),
    (1, "mother", "妈妈", "My mother is very kind.", "我妈妈很和蔼。"),
    (1, "father", "爸爸", "My father works hard.", "我爸爸工作很努力。"),
    (1, "sister", "姐姐；妹妹", "I have a little sister.", "我有一个妹妹。"),
    (1, "brother", "哥哥；弟弟", "My brother likes football.", "我哥哥喜欢足球。"),
    (1, "family", "家庭", "This is my family.", "这是我的家人。"),
    (1, "red", "红色的", "My bag is red.", "我的书包是红色的。"),
    (1, "blue", "蓝色的", "The sky is blue.", "天空是蓝色的。"),
    (1, "green", "绿色的", "The grass is green.", "草是绿色的。"),
    (1, "yellow", "黄色的", "The banana is yellow.", "香蕉是黄色的。"),
    (1, "black", "黑色的", "My cat is black.", "我的猫是黑色的。"),
    (1, "white", "白色的", "Snow is white.", "雪是白色的。"),
    (1, "one", "一", "I have one apple.", "我有一个苹果。"),
    (1, "two", "二", "There are two birds.", "有两只鸟。"),
    (1, "three", "三", "Three cats are here.", "三只猫在这里。"),
    (1, "four", "四", "I see four ducks.", "我看见四只鸭子。"),
    (1, "five", "五", "Five fingers on my hand.", "我手上有五根手指。"),
    (1, "six", "六", "I have six books.", "我有六本书。"),
    (1, "seven", "七", "Seven days in a week.", "一周有七天。"),
    (1, "eight", "八", "Eight stars at night.", "夜里有八颗星星。"),
    (1, "nine", "九", "Nine birds fly away.", "九只鸟飞走了。"),
    (1, "ten", "十", "I can count to ten.", "我能数到十。"),
    (1, "cat", "猫", "The cat is sleeping.", "猫正在睡觉。"),
    (1, "dog", "狗", "My dog is very cute.", "我的狗很可爱。"),
    (1, "bird", "鸟", "A bird is singing.", "一只鸟在唱歌。"),
    (1, "fish", "鱼", "The fish is in the water.", "鱼在水里。"),
    (1, "duck", "鸭子", "The duck can swim.", "鸭子会游泳。"),
    (1, "pig", "猪", "The pig is pink.", "猪是粉色的。"),
    (1, "bear", "熊", "The bear likes honey.", "熊喜欢蜂蜜。"),
    (1, "apple", "苹果", "I eat an apple every day.", "我每天吃一个苹果。"),
    (1, "banana", "香蕉", "The banana is sweet.", "香蕉很甜。"),
    (1, "orange", "橙子；橙色", "This orange is sweet.", "这个橙子很甜。"),
    (1, "milk", "牛奶", "I drink milk every morning.", "我每天早上喝牛奶。"),
    (1, "egg", "鸡蛋", "I have an egg for breakfast.", "我早餐吃一个鸡蛋。"),
    (1, "rice", "米饭", "We eat rice for lunch.", "我们午饭吃米饭。"),
    (1, "book", "书", "I read a book after school.", "放学后我读一本书。"),
    (1, "pen", "钢笔", "This pen is new.", "这支钢笔是新的。"),
    (1, "bag", "书包", "My bag is heavy.", "我的书包很重。"),
    (1, "desk", "课桌", "My book is on the desk.", "我的书在课桌上。"),
    (1, "chair", "椅子", "Please sit on the chair.", "请坐在椅子上。"),
    (1, "school", "学校", "I walk to school every morning.", "我每天早上走路去学校。"),
    (1, "teacher", "老师", "My teacher is very kind.", "我的老师很和蔼。"),
    (1, "student", "学生", "I am a student.", "我是一名学生。"),
    (1, "park", "公园", "We play in the park.", "我们在公园里玩。"),
]

WORDS += [
    # Grade 2
    (2, "pencil", "铅笔", "May I use your pencil?", "我能用一下你的铅笔吗？"),
    (2, "ruler", "尺子", "My ruler is twenty centimetres long.", "我的尺子长二十厘米。"),
    (2, "eraser", "橡皮", "Where is my eraser?", "我的橡皮在哪里？"),
    (2, "class", "班级；课", "Our class is very big.", "我们的班级很大。"),
    (2, "door", "门", "Please close the door.", "请关上门。"),
    (2, "window", "窗户", "Open the window, please.", "请打开窗户。"),
    (2, "wall", "墙", "The picture is on the wall.", "画在墙上。"),
    (2, "floor", "地板", "The floor is clean.", "地板很干净。"),
    (2, "cake", "蛋糕", "The cake is for my birthday.", "这个蛋糕是为我的生日准备的。"),
    (2, "bread", "面包", "I eat bread in the morning.", "我早上吃面包。"),
    (2, "water", "水", "Please drink more water.", "请多喝水。"),
    (2, "juice", "果汁", "I like orange juice.", "我喜欢橙汁。"),
    (2, "tea", "茶", "My father drinks tea.", "我爸爸喝茶。"),
    (2, "noodle", "面条", "The noodles are hot.", "面条很烫。"),
    (2, "meat", "肉", "The meat is on the plate.", "肉在盘子里。"),
    (2, "ice cream", "冰淇淋", "I want an ice cream.", "我想要一个冰淇淋。"),
    (2, "candy", "糖果", "The candy is very sweet.", "糖果很甜。"),
    (2, "hot", "热的", "The water is hot.", "水是热的。"),
    (2, "cold", "冷的", "It is cold today.", "今天很冷。"),
    (2, "warm", "温暖的", "Spring is warm.", "春天很温暖。"),
    (2, "cool", "凉爽的", "Autumn is cool.", "秋天很凉爽。"),
    (2, "sunny", "晴朗的", "It is a sunny day.", "这是个晴朗的日子。"),
    (2, "rainy", "下雨的", "It is rainy outside.", "外面在下雨。"),
    (2, "windy", "有风的", "It is windy today.", "今天有风。"),
    (2, "cloudy", "多云的", "The sky is cloudy.", "天空多云。"),
    (2, "snow", "雪", "The snow is white.", "雪是白色的。"),
    (2, "rain", "雨", "The rain is heavy.", "雨下得很大。"),
    (2, "sun", "太阳", "The sun is bright.", "太阳很明亮。"),
    (2, "moon", "月亮", "The moon is round tonight.", "今晚月亮是圆的。"),
    (2, "star", "星星", "Stars shine at night.", "星星在夜里发光。"),
    (2, "sky", "天空", "The sky is very high.", "天空很高。"),
    (2, "tree", "树", "The tree is tall.", "这棵树很高。"),
    (2, "flower", "花", "The flower is red.", "这朵花是红色的。"),
    (2, "grass", "草", "The grass is green.", "草是绿色的。"),
    (2, "leaf", "叶子", "A leaf falls down.", "一片叶子落下来。"),
    (2, "zoo", "动物园", "We go to the zoo today.", "我们今天去动物园。"),
    (2, "home", "家", "I am at home now.", "我现在在家。"),
    (2, "room", "房间", "My room is small.", "我的房间很小。"),
    (2, "bed", "床", "My bed is soft.", "我的床很软。"),
    (2, "clock", "时钟", "The clock is on the desk.", "钟在桌上。"),
    (2, "photo", "照片", "This is a photo of my family.", "这是我家人的照片。"),
    (2, "birthday", "生日", "Today is my birthday.", "今天是我的生日。"),
    (2, "gift", "礼物", "Thank you for the gift.", "谢谢你的礼物。"),
    (2, "party", "聚会", "We have a party today.", "我们今天有个聚会。"),
    (2, "happy", "高兴的", "I am very happy.", "我很高兴。"),
    (2, "sad", "难过的", "Do not be sad.", "别难过。"),
    (2, "big", "大的", "The box is big.", "这个盒子很大。"),
    (2, "small", "小的", "The cat is small.", "这只猫很小。"),
    (2, "long", "长的", "The ruler is long.", "尺子很长。"),
    (2, "short", "短的", "My pen is short.", "我的笔很短。"),
]

WORDS += [
    # Grade 3
    (3, "morning", "早晨", "I get up early in the morning.", "我早晨起得早。"),
    (3, "afternoon", "下午", "We have class in the afternoon.", "我们下午上课。"),
    (3, "evening", "傍晚", "We watch TV in the evening.", "我们傍晚看电视。"),
    (3, "night", "夜晚", "I sleep at night.", "我夜晚睡觉。"),
    (3, "day", "天；白天", "What a nice day!", "多好的一天！"),
    (3, "week", "星期；周", "There are seven days in a week.", "一周有七天。"),
    (3, "month", "月", "This month has thirty days.", "这个月有三十天。"),
    (3, "year", "年", "A year has twelve months.", "一年有十二个月。"),
    (3, "today", "今天", "Today is my birthday.", "今天是我的生日。"),
    (3, "tomorrow", "明天", "See you tomorrow.", "明天见。"),
    (3, "yesterday", "昨天", "Yesterday was Sunday.", "昨天是星期天。"),
    (3, "hungry", "饿的", "I am hungry now.", "我现在饿了。"),
    (3, "thirsty", "渴的", "I am thirsty, I want water.", "我渴了，我想喝水。"),
    (3, "tired", "累的", "I am tired after school.", "放学后我很累。"),
    (3, "angry", "生气的", "My father is not angry.", "我爸爸没有生气。"),
    (3, "tall", "高的", "My brother is tall.", "我哥哥很高。"),
    (3, "young", "年轻的", "The teacher looks young.", "这位老师看起来很年轻。"),
    (3, "old", "老的；旧的", "This bike is old.", "这辆自行车很旧。"),
    (3, "new", "新的", "I have a new book.", "我有一本新书。"),
    (3, "fast", "快的", "The car runs fast.", "汽车跑得很快。"),
    (3, "slow", "慢的", "The turtle is slow.", "乌龟很慢。"),
    (3, "strong", "强壮的", "He is strong and tall.", "他又强壮又高。"),
    (3, "clean", "干净的", "The room is clean.", "房间很干净。"),
    (3, "dirty", "脏的", "My shoes are dirty.", "我的鞋很脏。"),
    (3, "easy", "容易的", "The question is easy.", "这个问题很容易。"),
    (3, "hard", "困难的；硬的", "This question is hard.", "这个问题很难。"),
    (3, "beautiful", "美丽的", "The garden is beautiful.", "花园很美丽。"),
    (3, "cute", "可爱的", "The baby is cute.", "这个宝宝很可爱。"),
    (3, "funny", "有趣的", "The story is funny.", "这个故事很有趣。"),
    (3, "quiet", "安静的", "Please be quiet.", "请安静。"),
    (3, "loud", "响亮的", "The music is loud.", "音乐很响。"),
    (3, "open", "打开", "Please open your books.", "请打开你们的书。"),
    (3, "close", "关上", "Close the window, please.", "请关上窗户。"),
    (3, "start", "开始", "The class starts at eight.", "课八点开始。"),
    (3, "finish", "完成", "I finish my homework at seven.", "我七点完成作业。"),
    (3, "help", "帮助", "Can you help me?", "你能帮我吗？"),
    (3, "play", "玩；踢", "We play football after school.", "我们放学后踢足球。"),
    (3, "run", "跑", "I can run fast.", "我能跑得很快。"),
    (3, "jump", "跳", "The rabbit can jump.", "兔子会跳。"),
    (3, "swim", "游泳", "I swim in summer.", "我在夏天游泳。"),
    (3, "sing", "唱歌", "She likes to sing.", "她喜欢唱歌。"),
    (3, "dance", "跳舞", "They dance together.", "他们一起跳舞。"),
    (3, "draw", "画画", "I draw a picture.", "我画了一幅画。"),
    (3, "write", "写", "Write your name here.", "在这里写下你的名字。"),
    (3, "read", "读", "I read books every day.", "我每天读书。"),
    (3, "listen", "听", "Listen to the teacher.", "听老师讲。"),
    (3, "speak", "说", "Please speak slowly.", "请慢点说。"),
    (3, "walk", "走", "I walk to school.", "我走路去学校。"),
    (3, "look", "看", "Look at the blackboard.", "看黑板。"),
]

WORDS += [
    # Grade 4
    (4, "always", "总是", "She is always on time.", "她总是准时。"),
    (4, "usually", "通常", "I usually get up at six.", "我通常六点起床。"),
    (4, "often", "经常", "We often play together.", "我们经常一起玩。"),
    (4, "sometimes", "有时", "Sometimes I walk to school.", "我有时走路去学校。"),
    (4, "never", "从不", "He never eats candy.", "他从不吃糖。"),
    (4, "because", "因为", "I like it because it is nice.", "我喜欢它因为它很好。"),
    (4, "before", "在……之前", "Wash your hands before dinner.", "吃饭前洗手。"),
    (4, "after", "在……之后", "We play after class.", "课后我们玩。"),
    (4, "between", "在……之间", "The ball is between the chairs.", "球在椅子之间。"),
    (4, "behind", "在……后面", "The cat is behind the door.", "猫在门后。"),
    (4, "beside", "在……旁边", "Sit beside me, please.", "请坐在我旁边。"),
    (4, "under", "在……下面", "The dog is under the table.", "狗在桌子下面。"),
    (4, "above", "在……上方", "A plane flies above the clouds.", "飞机在云上方飞。"),
    (4, "inside", "在……里面", "The gift is inside the box.", "礼物在盒子里。"),
    (4, "outside", "在……外面", "It is cold outside.", "外面很冷。"),
    (4, "left", "左边", "The shop is on the left.", "商店在左边。"),
    (4, "right", "右边", "Turn right at the corner.", "在拐角处向右转。"),
    (4, "middle", "中间", "Stand in the middle, please.", "请站在中间。"),
    (4, "country", "国家", "China is a big country.", "中国是一个大国。"),
    (4, "city", "城市", "Beijing is a big city.", "北京是一座大城市。"),
    (4, "village", "村庄", "My grandma lives in a village.", "我奶奶住在村庄里。"),
    (4, "farm", "农场", "There are cows on the farm.", "农场里有奶牛。"),
    (4, "river", "河流", "The river is long.", "这条河很长。"),
    (4, "lake", "湖", "The lake is quiet.", "湖面很安静。"),
    (4, "mountain", "山", "We climb the mountain.", "我们爬山。"),
    (4, "sea", "海", "The sea is blue.", "大海是蓝色的。"),
    (4, "island", "岛", "There is an island in the sea.", "海里有一座岛。"),
    (4, "forest", "森林", "Many trees are in the forest.", "森林里有很多树。"),
    (4, "road", "道路", "The road is wide.", "这条路很宽。"),
    (4, "bridge", "桥", "We walk across the bridge.", "我们走过桥。"),
    (4, "building", "建筑物", "That building is tall.", "那栋楼很高。"),
    (4, "hospital", "医院", "My mother works in a hospital.", "我妈妈在医院工作。"),
    (4, "library", "图书馆", "I read in the library.", "我在图书馆看书。"),
    (4, "museum", "博物馆", "We visit the museum today.", "我们今天参观博物馆。"),
    (4, "restaurant", "餐馆", "We eat in a restaurant.", "我们在一家餐馆吃饭。"),
    (4, "supermarket", "超市", "I buy milk in the supermarket.", "我在超市买牛奶。"),
    (4, "bank", "银行", "The bank is near my home.", "银行在我家附近。"),
    (4, "station", "车站", "Wait at the station, please.", "请在车站等。"),
    (4, "airport", "机场", "The airport is far away.", "机场很远。"),
    (4, "ticket", "票", "I have a train ticket.", "我有一张火车票。"),
    (4, "map", "地图", "Look at the map.", "看看地图。"),
    (4, "travel", "旅行", "We travel by train.", "我们坐火车旅行。"),
    (4, "visit", "参观；拜访", "I visit my grandma on Sunday.", "我星期天去看奶奶。"),
    (4, "arrive", "到达", "We arrive at eight.", "我们八点到达。"),
    (4, "leave", "离开", "The bus leaves at nine.", "公交车九点离开。"),
    (4, "wait", "等待", "Please wait for me.", "请等等我。"),
    (4, "carry", "搬运", "Can you carry this box?", "你能搬这个箱子吗？"),
    (4, "choose", "选择", "Choose one colour.", "选一个颜色。"),
    (4, "remember", "记住", "Remember to bring your book.", "记得带上你的书。"),
]

WORDS += [
    # Grade 5
    (5, "dream", "梦想", "My dream is to be a doctor.", "我的梦想是当医生。"),
    (5, "idea", "主意；想法", "That is a good idea.", "那是个好主意。"),
    (5, "plan", "计划", "We have a plan for the holiday.", "我们对假期有个计划。"),
    (5, "problem", "问题", "Can you help me with this problem?", "你能帮我解决这个问题吗？"),
    (5, "answer", "答案", "Your answer is right.", "你的答案是对的。"),
    (5, "question", "问题；疑问", "May I ask a question?", "我可以问一个问题吗？"),
    (5, "story", "故事", "My mother tells me a story.", "妈妈给我讲一个故事。"),
    (5, "news", "消息；新闻", "That is good news.", "那是个好消息。"),
    (5, "message", "信息", "I send a message to my friend.", "我给朋友发一条信息。"),
    (5, "computer", "电脑", "I use a computer at home.", "我在家使用电脑。"),
    (5, "internet", "互联网", "The internet is very useful.", "互联网很有用。"),
    (5, "email", "电子邮件", "I write an email to my teacher.", "我给老师写一封电子邮件。"),
    (5, "picture", "图画", "The picture is beautiful.", "这幅画很美。"),
    (5, "movie", "电影", "We watch a movie tonight.", "我们今晚看电影。"),
    (5, "music", "音乐", "She likes music very much.", "她非常喜欢音乐。"),
    (5, "game", "游戏", "This game is fun.", "这个游戏很有趣。"),
    (5, "sport", "运动", "Sport is good for us.", "运动对我们有好处。"),
    (5, "football", "足球", "He plays football well.", "他足球踢得很好。"),
    (5, "basketball", "篮球", "We play basketball after school.", "我们放学后打篮球。"),
    (5, "team", "队伍", "Our team wins the game.", "我们队赢了比赛。"),
    (5, "match", "比赛", "The match starts at three.", "比赛三点开始。"),
    (5, "win", "赢", "I hope we win.", "我希望我们赢。"),
    (5, "lose", "输", "Do not be afraid to lose.", "不要害怕输。"),
    (5, "practice", "练习", "Practice makes perfect.", "熟能生巧。"),
    (5, "exercise", "锻炼", "I exercise every morning.", "我每天早上锻炼。"),
    (5, "healthy", "健康的", "Vegetables are healthy.", "蔬菜很健康。"),
    (5, "ill", "生病的", "My sister is ill today.", "我妹妹今天生病了。"),
    (5, "medicine", "药", "Take the medicine after dinner.", "晚饭后吃药。"),
    (5, "doctor", "医生", "The doctor is very kind.", "这位医生很和蔼。"),
    (5, "nurse", "护士", "The nurse helps the doctor.", "护士帮助医生。"),
    (5, "farmer", "农民", "The farmer works on the farm.", "农民在农场干活。"),
    (5, "worker", "工人", "My father is a worker.", "我爸爸是工人。"),
    (5, "driver", "司机", "The driver is very careful.", "这位司机很小心。"),
    (5, "scientist", "科学家", "I want to be a scientist.", "我想当科学家。"),
    (5, "engineer", "工程师", "My uncle is an engineer.", "我叔叔是工程师。"),
    (5, "cook", "厨师；烹饪", "My mother can cook well.", "我妈妈很会做饭。"),
    (5, "writer", "作家", "The writer tells a good story.", "这位作家讲了一个好故事。"),
    (5, "artist", "艺术家", "The artist draws a picture.", "这位艺术家画了一幅画。"),
    (5, "singer", "歌手", "The singer has a nice voice.", "这位歌手嗓音很好。"),
    (5, "actor", "演员", "The actor is very famous.", "这位演员很有名。"),
    (5, "build", "建造", "They build a new school.", "他们建了一所新学校。"),
    (5, "create", "创造", "We create a new game.", "我们创造了一个新游戏。"),
    (5, "invent", "发明", "Who invented the phone?", "谁发明了电话？"),
    (5, "discover", "发现", "We discover a small lake.", "我们发现了一个小湖。"),
    (5, "protect", "保护", "We should protect the trees.", "我们应该保护树木。"),
    (5, "save", "节省；拯救", "Save water, please.", "请节约用水。"),
    (5, "share", "分享", "I share my lunch with him.", "我和他分享我的午餐。"),
    (5, "collect", "收集", "I collect stamps.", "我收集邮票。"),
]

WORDS += [
    # Grade 6
    (6, "future", "未来", "I think about my future.", "我思考我的未来。"),
    (6, "past", "过去", "In the past, life was hard.", "过去生活很艰难。"),
    (6, "present", "现在；礼物", "We should enjoy the present.", "我们应该享受现在。"),
    (6, "change", "改变", "The weather can change fast.", "天气变化很快。"),
    (6, "chance", "机会", "Give me a chance, please.", "请给我一次机会。"),
    (6, "choice", "选择", "You have a choice.", "你有一个选择。"),
    (6, "decision", "决定", "It is a good decision.", "这是个好决定。"),
    (6, "reason", "原因", "Tell me the reason.", "告诉我原因。"),
    (6, "result", "结果", "The result is good.", "结果很好。"),
    (6, "success", "成功", "Hard work brings success.", "努力带来成功。"),
    (6, "fail", "失败", "Do not fear to fail.", "不要害怕失败。"),
    (6, "effort", "努力", "Every effort is useful.", "每一分努力都有用。"),
    (6, "courage", "勇气", "He has great courage.", "他有很大的勇气。"),
    (6, "honest", "诚实的", "An honest boy never lies.", "诚实的孩子不说谎。"),
    (6, "polite", "有礼貌的", "Please be polite to others.", "请对别人有礼貌。"),
    (6, "careful", "小心的；仔细的", "Be careful with the knife.", "用刀要小心。"),
    (6, "patient", "有耐心的", "The teacher is patient.", "这位老师很有耐心。"),
    (6, "friendly", "友好的", "Our neighbours are friendly.", "我们的邻居很友好。"),
    (6, "helpful", "有帮助的", "Your advice is helpful.", "你的建议很有帮助。"),
    (6, "dangerous", "危险的", "It is dangerous to play here.", "在这里玩很危险。"),
    (6, "safe", "安全的", "This road is safe.", "这条路很安全。"),
    (6, "difficult", "困难的", "The task is difficult.", "这项任务很困难。"),
    (6, "possible", "可能的", "It is possible to finish today.", "今天完成是可能的。"),
    (6, "important", "重要的", "Health is important.", "健康很重要。"),
    (6, "interesting", "有趣的", "The book is interesting.", "这本书很有趣。"),
    (6, "exciting", "令人兴奋的", "The match is exciting.", "比赛令人兴奋。"),
    (6, "boring", "无聊的", "The movie is boring.", "这部电影很无聊。"),
    (6, "terrible", "糟糕的", "The weather is terrible.", "天气很糟糕。"),
    (6, "wonderful", "精彩的", "We had a wonderful day.", "我们度过了精彩的一天。"),
    (6, "amazing", "令人惊奇的", "The view is amazing.", "景色令人惊叹。"),
    (6, "nervous", "紧张的", "I am nervous before the test.", "考试前我很紧张。"),
    (6, "proud", "自豪的", "My parents are proud of me.", "我父母为我自豪。"),
    (6, "sorry", "抱歉的", "I am sorry for being late.", "很抱歉我迟到了。"),
    (6, "thankful", "感激的", "I am thankful for your help.", "我感激你的帮助。"),
    (6, "agree", "同意", "I agree with your idea.", "我同意你的想法。"),
    (6, "refuse", "拒绝", "He refuses to give up.", "他拒绝放弃。"),
    (6, "accept", "接受", "I accept your gift.", "我接受你的礼物。"),
    (6, "invite", "邀请", "I invite you to my party.", "我邀请你参加我的聚会。"),
    (6, "explain", "解释", "Please explain the rule.", "请解释这条规则。"),
    (6, "describe", "描述", "Describe your family, please.", "请描述你的家人。"),
    (6, "compare", "比较", "Compare the two pictures.", "比较这两幅图。"),
    (6, "improve", "提高", "I want to improve my English.", "我想提高我的英语。"),
    (6, "continue", "继续", "Let us continue the story.", "让我们继续这个故事。"),
    (6, "prepare", "准备", "We prepare for the test.", "我们为考试做准备。"),
    (6, "celebrate", "庆祝", "We celebrate the New Year.", "我们庆祝新年。"),
    (6, "imagine", "想象", "Imagine a quiet garden.", "想象一座安静的花园。"),
    (6, "respect", "尊重", "We respect our teachers.", "我们尊重老师。"),
]


def c_string(value: str | None) -> str:
    if value is None:
        return "NULL"
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def grade_groups(rows: list[tuple]) -> list[list[tuple]]:
    groups: list[list[tuple]] = [[] for _ in range(GRADE_COUNT)]
    for row in rows:
        grade = row[0]
        if not 1 <= grade <= GRADE_COUNT:
            raise SystemExit(f"grade out of range: {grade}")
        groups[grade - 1].append(row)
    for index, group in enumerate(groups, start=1):
        if not group:
            raise SystemExit(f"grade {index} has no cards")
    return groups


def emit_cards() -> tuple[str, list[list[tuple]], list[list[tuple]]]:
    poem_groups = grade_groups(POEMS)
    word_groups = grade_groups([(g, w, c, None, e, ec) for g, w, c, e, ec in WORDS])

    body: list[str] = []
    body.append("/* Generated by tools/generate_ecard_cards.py. Do not edit by hand. */")
    body.append('#include "ecard_cards.h"')
    body.append("")
    body.append("#include <stddef.h>")
    body.append("")

    for grade, group in enumerate(poem_groups, start=1):
        body.append(f"/* Grade {grade} poems: {len(group)} */")
        body.append(f"static const ecard_card_t k_poems_{grade}[] = {{")
        for _, front, back, meta, explain in group:
            body.append(
                f"    {{ {c_string(front)}, {c_string(back)}, {c_string(meta)}, "
                f"{c_string(explain)}, NULL, NULL }},")
        body.append("};")
        body.append("")

    for grade, group in enumerate(word_groups, start=1):
        body.append(f"/* Grade {grade} words: {len(group)} */")
        body.append(f"static const ecard_card_t k_words_{grade}[] = {{")
        for _, word, chinese, _meta, example, example_cn in group:
            body.append(
                f"    {{ {c_string(word)}, {c_string(chinese)}, NULL, NULL, "
                f"{c_string(example)}, {c_string(example_cn)} }},")
        body.append("};")
        body.append("")

    body.append("static const ecard_card_t *const k_poem_tables[] = {")
    for grade in range(1, GRADE_COUNT + 1):
        body.append(f"    k_poems_{grade},")
    body.append("};")
    body.append("")
    body.append("static const uint8_t k_poem_counts[] = {")
    body.append("    " + ", ".join(str(len(g)) for g in poem_groups) + ",")
    body.append("};")
    body.append("")
    body.append("static const ecard_card_t *const k_word_tables[] = {")
    for grade in range(1, GRADE_COUNT + 1):
        body.append(f"    k_words_{grade},")
    body.append("};")
    body.append("")
    body.append("static const uint8_t k_word_counts[] = {")
    body.append("    " + ", ".join(str(len(g)) for g in word_groups) + ",")
    body.append("};")
    body.append("")

    offsets: list[int] = []
    running = 0
    for group in word_groups:
        offsets.append(running)
        running += len(group)
    body.append(f"#define ECARD_WORD_TOTAL {running}")
    body.append("")
    body.append("static const uint16_t k_word_offsets[] = {")
    body.append("    " + ", ".join(str(offset) for offset in offsets) + ",")
    body.append("};")
    body.append("")
    body.append('''const ecard_card_t *ecard_cards_poems(uint8_t grade, uint8_t *count) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) return NULL;
    if (count) *count = k_poem_counts[grade - 1];
    return k_poem_tables[grade - 1];
}

const ecard_card_t *ecard_cards_words(uint8_t grade, uint8_t *count) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) return NULL;
    if (count) *count = k_word_counts[grade - 1];
    return k_word_tables[grade - 1];
}

uint16_t ecard_cards_word_offset(uint8_t grade) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) return 0;
    return k_word_offsets[grade - 1];
}

uint16_t ecard_cards_word_total(void) {
    return ECARD_WORD_TOTAL;
}''')
    body.append("")
    return "\n".join(body), poem_groups, word_groups


def encode_adpcm(samples: list[int]) -> bytes:
    predicted = 0
    index = 0
    out = bytearray()
    pending_low = None
    for sample in samples:
        diff = sample - predicted
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff
        step = STEP_TABLE[index]
        delta = step >> 3
        if diff >= step:
            nibble |= 4
            diff -= step
            delta += step
        if diff >= (step >> 1):
            nibble |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= (step >> 2):
            nibble |= 1
            delta += step >> 2
        predicted += -delta if (nibble & 8) else delta
        predicted = max(-32768, min(32767, predicted))
        index += INDEX_TABLE[nibble]
        index = max(0, min(88, index))
        if pending_low is None:
            pending_low = nibble & 0x0F
        else:
            out.append(pending_low | ((nibble & 0x0F) << 4))
            pending_low = None
    if pending_low is not None:
        out.append(pending_low)
    return bytes(out)


def trim_silence(samples: list[int], floor: int = SILENCE_FLOOR) -> list[int]:
    start = 0
    end = len(samples)
    while start < end and abs(samples[start]) < floor:
        start += 1
    while end > start and abs(samples[end - 1]) < floor:
        end -= 1
    pad = int(WORD_RATE * 0.1)
    start = max(0, start - pad)
    end = min(len(samples), end + pad)
    return samples[start:end]


def synthesize(text: str, tmp: Path) -> list[int]:
    aiff = tmp / "clip.aiff"
    wav = tmp / "clip.wav"
    subprocess.run(["say", "-v", EN_VOICE, "-r", str(EN_RATE), "-o", str(aiff), text],
                   check=True, capture_output=True)
    subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{WORD_RATE}", "-c", "1",
                    str(aiff), str(wav)], check=True, capture_output=True)
    with wave.open(str(wav), "rb") as handle:
        frames = handle.readframes(handle.getnframes())
    samples = [int.from_bytes(frames[i:i + 2], "little", signed=True)
               for i in range(0, len(frames), 2)]
    return trim_silence(samples)


def emit_speech(word_groups: list[list[tuple]]) -> tuple[str, int]:
    clips: list[bytes] = []
    with tempfile.TemporaryDirectory() as raw_tmp:
        tmp = Path(raw_tmp)
        for group in word_groups:
            for _, word, _cn, _meta, _ex, _ec in group:
                clips.append(encode_adpcm(synthesize(word, tmp)))
                print(f"  word clip {len(clips) - 1}: {len(clips[-1])} B", file=sys.stderr)

    body: list[str] = []
    body.append("/* Generated by tools/generate_ecard_cards.py. Do not edit by hand. */")
    body.append('#include "ecard_speech.h"')
    body.append("")
    body.append(f"#define ECARD_SPEECH_WORD_COUNT {len(clips)}")
    body.append("")
    for index, clip in enumerate(clips):
        body.append(f"/* word[{index}] {len(clip)} bytes */")
        body.append(f"static const uint8_t word_clip_{index}[] = {{")
        for offset in range(0, len(clip), 20):
            body.append("    " + ", ".join(f"0x{byte:02X}" for byte in clip[offset:offset + 20]) + ",")
        body.append("};")
        body.append("")
    body.append("")
    body.append("uint16_t ecard_speech_word_total(void) {")
    body.append("    return ECARD_SPEECH_WORD_COUNT;")
    body.append("}")
    body.append("")
    body.append("const ecard_speech_clip_t ecard_speech_word[ECARD_SPEECH_WORD_COUNT] = {")
    for index, clip in enumerate(clips):
        body.append(f"    {{ word_clip_{index}, {len(clip)}, 1 }},")
    body.append("};")
    body.append("")
    return "\n".join(body), sum(len(c) for c in clips)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-audio", action="store_true", help="only write the text tables")
    args = parser.parse_args()

    total_words = sum(1 for _ in WORDS)
    total_poems = sum(1 for _ in POEMS)
    print(f"poems: {total_poems}, words: {total_words}", file=sys.stderr)

    source, poem_groups, word_groups = emit_cards()
    CARDS_OUT.write_text(source, encoding="utf-8")
    print(f"wrote {CARDS_OUT}", file=sys.stderr)

    if args.no_audio:
        return 0

    if not (shutil.which("say") and shutil.which("afconvert")):
        raise SystemExit("audio generation requires macOS `say` and `afconvert`")

    source, speech_size = emit_speech(word_groups)
    SPEECH_OUT.write_text(source, encoding="utf-8")
    print(f"wrote {SPEECH_OUT} ({speech_size} bytes of ADPCM)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
