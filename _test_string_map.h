#pragma once
#include "_test.h"
#include "string_map.h"
#include "arena_stack.h"
#include "allocator_debug.h"

INTERNAL String_Builder generate_random_text(Allocator* alloc, isize word_count, String separator, bool capitilize, String postfix);

INTERNAL void test_string_unit()
{
	SCRATCH_ARENA(arena)
	{
		Debug_Allocator debug = {0};
		debug_allocator_init(&debug, arena.alloc, DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK);
		{
			String_Map table = {0};
			string_map_init(&table, debug.alloc, sizeof(i32));

			const Hash_String a = HSTRING("AAAA");
			const Hash_String b = HSTRING("BBBB");
			const Hash_String c = HSTRING("CCCC");

			string_map_assign_or_insert(&table, a, VPTR(i32, 1));
			string_map_assign_or_insert(&table, b, VPTR(i32, 2));
			string_map_assign_or_insert(&table, a, VPTR(i32, 3));

			TEST(table.len == 2);
			TEST(table.max_collision_count == 0);
			{
				String_Map_Found af = string_map_find(&table, a);
				String_Map_Found bf = string_map_find(&table, b);

				TEST(af.index != -1 && hash_string_is_equal(af.key, a) && *(i32*) af.value == 3);
				TEST(bf.index != -1 && hash_string_is_equal(bf.key, b) && *(i32*) bf.value == 2);
			}
			
			string_map_assign_or_insert(&table, c, VPTR(i32, 3));
			string_map_insert(&table, a, VPTR(i32, 3));
			string_map_insert(&table, a, VPTR(i32, 4));
			string_map_insert(&table, a, VPTR(i32, 6));
			string_map_insert(&table, a, VPTR(i32, 7));
			
			TEST(table.len == 7);
			TEST(table.max_collision_count == 4);

			string_map_assign_or_insert(&table, HSTRING("Hello"), VPTR(i32, 4));
			string_map_insert(&table, HSTRING("Hello"), VPTR(i32, 40));
			string_map_remove(&table, HSTRING("Hello"));

			string_map_deinit(&table);
		}
		debug_allocator_deinit(&debug);
	}
}

INTERNAL void test_string_map(f64 max_seconds)
{
	(void) max_seconds;
	SCRATCH_ARENA(arena)
	{
		for(isize i = 0; i < 100; i++)
			LOG_INFO("RAND", "%s", generate_random_text(arena.alloc, 5, STRING(" "), true, STRING(".")).data); 

		LOG_HERE;
	}

	test_string_unit();
}

#define MOST_FREQUENT_WORDS \
	X("the",50033612) X("be",32394756) X("and",24778098) X("a",24225478) X("of",23159162) X("to",16770155) X("in",15670692) \
	X("i",14217601) X("you",12079413) X("it",11042044) X("have",10514314) X("to",9232572) X("that",8319512) X("for",8194970) \
	X("do",8186412) X("he",6467470) X("with",6442861) X("on",6080156) X("this",5541440) X("n't",5285354) X("we",5180711) \
	X("that",5002963) X("not",4655980) X("but",4523086) X("they",4503650) X("say",4096416) X("at",4024079) X("what",3807502) \
	X("his",3718978) X("from",3711425) X("go",3546732) X("or",3420339) X("by",3372222) X("get",3347615) X("she",3188078) \
	X("my",3106939) X("can",3091046) X("as",2946119) X("know",2761628) X("if",2709809) X("me",2638743) X("your",2577505) \
	X("all",2503556) X("who",2493429) X("about",2427703) X("their",2417058) X("will",2372215) X("so",2369749) X("would",2349400) \
	X("make",2290830) X("just",2270900) X("up",2108756) X("think",2077762) X("time",2018725) X("there",1980173) X("see",1958700) \
	X("her",1931189) X("as",1880190) X("out",1828593) X("one",1816593) X("come",1802158) X("people",1800205) X("take",1768822) \
	X("year",1729962) X("him",1717209) X("them",1701589) X("some",1684262) X("want",1671524) X("how",1666469) X("when",1650353) \
	X("which",1613281) X("now",1601991) X("like",1583444) X("other",1539952) X("could",1529795) X("our",1467955) X("into",1461573) \
	X("here",1413594) X("then",1344434) X("than",1342798) X("look",1338475) X("way",1260011) X("more",1248955) X("these",1223310) \
	X("no",1206112) X("thing",1202004) X("well",1189096) X("because",1167024) X("also",1142799) X("two",1139973) X("use",1126042) \
	X("tell",1119692) X("good",1111721) X("first",1101803) X("man",1091176) X("day",1068902) X("find",1051936) X("give",1048189) \
	X("more",1037966) X("new",1017175) X("one",999446) X("us",992207) X("any",981535) X("those",964458) X("very",963552) \
	X("her",959780) X("need",945498) X("back",938649) X("there",932354) X("should",920908) X("even",920346) X("only",905093) \
	X("many",903833) X("really",895900) X("work",854095) X("life",852257) X("why",832911) X("right",830729) X("down",820294) \
	X("on",818739) X("try",795248) X("let",780929) X("something",779903) X("too",771375) X("call",768117) X("woman",759817) \
	X("may",757742) X("still",757548) X("through",752325) X("mean",748840) X("after",745154) X("never",744608) X("no",743203) \
	X("world",732511) X("in",726590) X("feel",722826) X("yeah",703928) X("great",696589) X("last",692989) X("child",685426) \
	X("oh",685234) X("over",679617) X("ask",676596) X("when",669530) X("as",662376) X("school",660191) X("state",638012) \
	X("much",636642) X("talk",635614) X("out",634959) X("keep",626487) X("leave",622651) X("put",616952) X("like",614691) \
	X("help",606887) X("big",600364) X("where",597351) X("same",592441) X("all",591610) X("own",579172) X("while",579023) \
	X("start",578246) X("three",570885) X("high",567720) X("every",567233) X("another",565094) X("become",561963) X("most",561354) \
	X("between",558517) X("happen",552797) X("family",544520) X("over",538724) X("president",538319) X("old",537424) X("yes",537066)\
	X("house",537037) X("show",536889) X("again",530706) X("student",530196) X("so",523232) X("seem",522940) X("might",521992) \
	X("part",517693) X("hear",516483) X("its",511922) X("place",508803) X("problem",504175) X("where",500755) X("believe",500511) \
	X("country",499369) X("always",492943) X("week",484834) X("point",484094) X("hand",481332) X("off",479459) X("play",478740) \
	X("turn",477668) X("few",472804) X("group",470971) X("such",468655) X("against",467705) X("run",465066) X("guy",464082) \
	X("about",462995) X("case",458383) X("question",457301) X("work",456169) X("night",452094) X("live",450380) X("game",445149) \
	X("number",444412) X("write",439865) X("bring",439445) X("without",438567) X("money",437583) X("lot",437545) X("most",435849) \
	X("book",435387) X("system",435303) X("government",434971) X("next",433864) X("city",433843) X("company",432469) \
	X("story",432184) X("today",431562) X("job",430995) X("move",430324) X("must",427796) X("bad",426558) X("friend",423755) \
	X("during",423624) X("begin",421878) X("love",417532) X("each",414659) X("hold",413837) X("different",413578) \
	X("american",410698) X("little",404660) X("before",402959) X("ever",402590) X("word",402216) X("fact",399574) X("right",389906) \
	X("read",386137) X("anything",384508) X("nothing",383744) X("sure",383701) X("small",382563) X("month",381833) \
	X("program",375708) X("maybe",374325) X("right",373934) X("under",373903) X("business",373744) X("home",370758) X("kind",367584)\
	X("stop",366567) X("pay",365255) X("study",364915) X("since",363805) X("issue",362137) X("name",361916) X("idea",358006) \
	X("room",357682) X("percent",357515) X("far",357192) X("away",355515) X("law",354958) X("actually",353857) X("large",353703) \
	X("though",352608) X("provide",351941) X("lose",351650) X("power",351483) X("kid",351451) X("war",350575) X("understand",349141)\
	X("head",348664) X("mother",348470) X("real",348239) X("best",348202) X("team",348063) X("eye",347153) X("long",345462) \
	X("long",345005) X("side",342841) X("water",342574) X("young",341002) X("wait",339926) X("okay",339699) X("both",338454) \
	X("yet",338408) X("after",334050) X("meet",333954) X("service",332313) X("area",331866) X("important",331646) X("person",330987)\
	X("hey",330236) X("thank",330129) X("much",329230) X("someone",328998) X("end",328699) X("change",327637) X("however",326015) \
	X("only",325917) X("around",324064) X("hour",323999) X("everything",323760) X("national",321960) X("four",319834) \
	X("line",319492) X("girl",319120) X("around",318942) X("watch",318772) X("until",318392) X("father",318173) X("sit",315022) \
	X("create",314631) X("information",314341) X("car",313437) X("learn",312649) X("least",311569) X("already",311502) \
	X("kill",307305) X("minute",306541) X("party",304325) X("include",304197) X("stand",303462) X("together",302942) \
	X("back",302390) X("follow",300266) X("health",300262) X("remember",298221) X("often",295709) X("reason",295577) \
	X("speak",295523) X("ago",294921) X("set",293144) X("black",293101) X("member",292596) X("community",290489) X("once",290446) \
	X("social",290372) X("news",289175) X("allow",288559) X("win",288308) X("body",287817) X("lead",285740) X("continue",282352) \
	X("whether",280973) X("enough",280789) X("spend",280371) X("level",279770) X("able",279559) X("political",279410) \
	X("almost",279393) X("boy",279283) X("university",279132) X("before",276286) X("stay",275504) X("add",275233) X("later",274178) \
	X("change",274132) X("five",270274) X("probably",269732) X("center",268821) X("among",267798) X("face",266458) \
	X("public",265061) X("die",264537) X("food",262697) X("else",261804) X("history",261045) X("buy",260201) X("result",259961) \
	X("morning",259129) X("off",258852) X("parent",258181) X("office",258157) X("course",257245) X("send",256309) \
	X("research",255123) X("walk",253671) X("door",252623) X("white",251664) X("several",251543) X("court",250891) X("home",250331) \
	X("grow",248803) X("better",247453) X("open",247043) X("moment",246362) X("including",245445) X("consider",244644) \
	X("both",244397) X("such",244165) X("little",244049) X("within",243714) X("second",243485) X("late",242755) X("street",242552) \
	X("free",242338) X("better",241827) X("everyone",241313) X("policy",240181) X("table",238887) X("sorry",237541) X("care",237259)\
	X("low",237027) X("human",236187) X("please",236175) X("hope",235945) X("TRUE",235467) X("process",235304) X("teacher",234642) \
	X("data",234516) X("offer",234189) X("death",233153) X("whole",233110) X("experience",232376) X("plan",231629) X("easy",231262) \
	X("education",231036) X("build",230071) X("expect",229855) X("fall",229161) X("himself",228757) X("age",228610) X("hard",228234)\
	X("sense",226539) X("across",226402) X("show",225884) X("early",224665) X("college",224634) X("music",222767) X("appear",221287)\
	X("mind",220441) X("class",219160) X("police",219041) X("use",218006) X("effect",217999) X("season",217695) X("tax",217131) \
	X("heart",216345) X("son",216216) X("art",215981) X("possible",213803) X("serve",213511) X("break",213364) X("although",212390) \
	X("end",212174) X("market",210848) X("even",210702) X("air",210307) X("force",210010) X("require",209616) X("foot",209334) \
	X("up",209243) X("listen",208819) X("agree",208506) X("according",208405) X("anyone",207907) X("baby",207577) X("wrong",206856) \
	X("love",206700) X("cut",205886) X("decide",205447) X("republican",204793) X("full",204252) X("behind",203530) X("pass",203033) \
	X("interest",202642) X("sometimes",201833) X("security",201542) X("eat",201315) X("report",201020) X("control",200879) \
	X("rate",200828) X("local",200668) X("suggest",200560) X("report",200188) X("nation",200021) X("sell",198982) X("action",198530)\
	X("support",198185) X("wife",197306) X("decision",196426) X("receive",196239) X("value",195639) X("base",195474) \
	X("pick",195443) X("phone",194941) X("thanks",194839) X("event",194748) X("drive",194360) X("strong",193876) X("reach",193684) \
	X("remain",193577) X("explain",193276) X("site",193251) X("hit",192969) X("pull",192457) X("church",191872) X("model",191448) \
	X("perhaps",191398) X("relationship",191344) X("six",191200) X("fine",190779) X("movie",190773) X("field",190489) \
	X("raise",190353) X("less",190003) X("player",189280) X("couple",189272) X("million",188521) X("themselves",188414) \
	X("record",187057) X("especially",186130) X("difference",185310) X("light",185287) X("development",185238) X("federal",185144) \
	X("former",185057) X("role",184483) X("pretty",183711) X("myself",183232) X("view",182417) X("price",181918) X("effort",181751) \
	X("nice",181568) X("quite",181432) X("along",181372) X("voice",181328) X("finally",181233) X("department",181181) \
	X("either",181133) X("toward",180784) X("leader",180573) X("because",178859) X("photo",177976) X("wear",177786) \
	X("space",177126) X("project",177076) X("return",176787) X("position",176578) X("special",176515) X("million",175586) \
	X("film",175442) X("need",175440) X("major",175432) X("type",173975) X("town",173905) X("article",173819) X("road",173413) \
	X("form",173161) X("chance",172820) X("drug",172756) X("economic",172153) X("situation",171026) X("choose",170716) \
	X("practice",170654) X("cause",170629) X("happy",170573) X("science",170488) X("join",170302) X("teach",169673) \
	X("early",169653) X("develop",168894) X("share",168892) X("yourself",168841) X("carry",168820) X("clear",168587) \
	X("brother",167417) X("matter",167351) X("dead",167302) X("image",167053) X("star",167036) X("cost",166775) X("simply",166556) \
	X("post",166265) X("society",165769) X("picture",165703) X("piece",165538) X("paper",165070) X("energy",164622) \
	X("personal",164270) X("building",164119) X("military",163555) X("open",163231) X("doctor",163129) X("activity",162943) \
	X("exactly",162663) X("american",162614) X("media",162177) X("miss",162066) X("evidence",162050) X("product",161999) \
	X("realize",161762) X("save",161469) X("arm",161185) X("technology",160819) X("catch",160594) X("comment",160451) \
	X("look",160145) X("term",160014) X("color",160011) X("cover",159606) X("describe",159521) X("guess",159454) X("choice",159277) \
	X("source",158588) X("mom",158511) X("soon",158194) X("director",158028) X("international",157724) X("rule",157468) \
	X("campaign",157373) X("ground",156706) X("election",156532) X("face",156455) X("uh",156333) X("check",155591) X("page",154863) \
	X("fight",154643) X("itself",154505) X("test",154189) X("patient",154125) X("produce",154082) X("certain",154059) \
	X("whatever",153872) X("half",153688) X("video",153563) X("support",152769) X("throw",152388) X("third",152339) X("care",152255)\
	X("rest",151864) X("recent",151697) X("available",151406) X("step",151394) X("ready",151349) X("opportunity",151226) \
	X("official",150423) X("oil",150410) X("call",149896) X("organization",149261) X("character",148825) X("single",148796) \
	X("current",148387) X("likely",148216) X("county",148165) X("future",147923) X("dad",147520) X("whose",147491) X("less",147290) \
	X("shoot",147137) X("industry",146746) X("second",146634) X("list",146495) X("general",146275) X("stuff",145992) \
	X("figure",145812) X("attention",145669) X("forget",145557) X("risk",145334) X("no",144919) X("focus",144896) X("short",144842) \
	X("fire",144750) X("dog",144648) X("red",144046) X("hair",143875) X("point",143750) X("condition",143732) X("wall",143389) \
	X("daughter",142747) X("before",142673) X("deal",142589) X("author",142352) X("truth",142017) X("upon",141420) \
	X("husband",141289) X("period",141173) X("series",140231) X("order",140025) X("officer",139938) X("close",139704) \
	X("land",139579) X("note",139111) X("computer",139082) X("thought",139016) X("economy",138752) X("goal",138693) X("bank",138673)\
	X("behavior",138400) X("sound",138386) X("deal",138197) X("certainly",138162) X("nearly",138034) X("increase",137709) \
	X("act",137668) X("north",137666) X("well",137599) X("blood",137579) X("culture",137140) X("medical",136850) X("ok",136788) \
	X("everybody",136787) X("top",136658) X("difficult",136474) X("close",136431) X("language",136124) X("window",136043) \
	X("response",135942) X("population",135733) X("lie",135712) X("tree",135666) X("park",135440) X("worker",135244) \
	X("draw",135022) X("plan",135011) X("drop",134220) X("push",134206) X("earth",134202) X("cause",133998) X("per",133974) \
	X("private",133907) X("tonight",133341) X("race",133182) X("than",133168) X("letter",132828) X("other",132728) X("gun",132572) \
	X("simple",132004) X("course",131911) X("wonder",131649) X("involve",131275) X("hell",131265) X("poor",131105) X("each",130754) \
	X("answer",130534) X("nature",130378) X("administration",130297) X("common",130294) X("no",130206) X("hard",129919) \
	X("message",129799) X("song",129648) X("enjoy",129575) X("similar",129272) X("congress",128923) X("attack",128893) \
	X("past",128832) X("hot",128310) X("seek",128234) X("amount",128075) X("analysis",128029) X("store",127984) X("defense",127924) \
	X("bill",127919) X("like",127828) X("cell",127818) X("away",127427) X("performance",127318) X("hospital",127317) X("bed",127232)\
	X("board",127202) X("protect",126672) X("century",126668) X("summer",126418) X("material",126254) X("individual",125578) \
	X("recently",125532) X("example",125442) X("represent",125330) X("fill",125249) X("state",125064) X("place",124900) \
	X("animal",124835) X("fail",124675) X("factor",123915) X("natural",123788) X("sir",123629) X("agency",123524) \
	X("usually",123303) X("significant",123040) X("help",123017) X("ability",122943) X("mile",122899) X("statement",122852) \
	X("entire",122397) X("democrat",122325) X("floor",122291) X("serious",122251) X("career",122177) X("dollar",121984) \
	X("vote",121578) X("sex",121352) X("compare",120978) X("south",120894) X("forward",120845) X("subject",120773) \
	X("financial",120726) X("identify",120690) X("beautiful",120679) X("decade",120539) X("bit",120467) X("reduce",120399) \
	X("sister",120346) X("quality",120236) X("quickly",120136) X("act",119650) X("press",119485) X("worry",119256) \
	X("accept",119245) X("enter",119119) X("mention",119033) X("sound",119028) X("thus",118653) X("plant",118323) \
	X("movement",118118) X("scene",118003) X("section",117170) X("treatment",116893) X("wish",116884) X("benefit",116806) \
	X("interesting",116773) X("west",116683) X("candidate",116347) X("approach",116173) X("determine",116138) X("resource",116078) \
	X("claim",116020) X("answer",115956) X("prove",115910) X("sort",115486) X("enough",115462) X("size",115388) X("somebody",115363)\
	X("knowledge",115356) X("rather",115104) X("hang",114959) X("sport",114894) X("tv",114859) X("loss",114630) X("argue",114425) \
	X("left",114320) X("note",114251) X("meeting",114209) X("skill",113912) X("card",113472) X("feeling",113414) X("despite",113257)\
	X("degree",113046) X("crime",112978) X("that",112903) X("sign",112603) X("occur",112575) X("imagine",112572) X("vote",112405) \
	X("near",112214) X("king",112088) X("box",112035) X("present",111731) X("figure",111647) X("seven",111646) X("foreign",111509) \
	X("laugh",111440) X("disease",111433) X("lady",111384) X("beyond",111221) X("discuss",111181) X("finish",111094) \
	X("design",111026) X("concern",110976) X("ball",110770) X("east",110589) X("recognize",110405) X("apply",110328) \
	X("prepare",110266) X("network",110210) X("huge",110088) X("success",110030) X("district",109941) X("cup",109915) \
	X("name",109851) X("physical",109576) X("growth",109567) X("rise",109035) X("hi",108665) X("standard",107818) X("force",107636) \
	X("sign",107633) X("fan",107631) X("theory",107486) X("staff",107396) X("hurt",107262) X("legal",107134) X("september",106932) \
	X("set",106920) X("outside",106572) X("et",106546) X("strategy",106236) X("clearly",105965) X("property",105851) X("lay",105812)\
	X("final",105688) X("authority",105586) X("perfect",105560) X("method",105544) X("region",105382) X("since",105367) \
	X("impact",105330) X("indicate",105304) X("safe",105273) X("committee",105133) X("supposed",105037) X("dream",104797) \
	X("training",104563) X("shit",104498) X("central",104403) X("option",104245) X("eight",104060) X("particularly",104018) \
	X("completely",103941) X("opinion",103920) X("main",103803) X("ten",103677) X("interview",103566) X("exist",103550) \
	X("remove",103480) X("dark",103384) X("play",103352) X("union",102805) X("professor",102741) X("pressure",102669) \
	X("purpose",102642) X("stage",102611) X("blue",102172) X("herself",102154) X("sun",102043) X("pain",101918) X("artist",101861) \
	X("employee",101818) X("avoid",101794) X("account",101683) X("release",101668) X("fund",101503) X("environment",101479) \
	X("treat",101438) X("specific",101222) X("version",101091) X("shot",101016) X("hate",100757) X("reality",100700) \
	X("visit",100463) X("club",100411) X("justice",100360) X("river",100270) X("brain",100235) X("memory",100235) X("rock",100028) \
	X("talk",99986) X("camera",99855) X("global",99848) X("various",99838) X("arrive",99819) X("notice",99709) X("bit",99520) \
	X("detail",99477) X("challenge",99455) X("argument",99403) X("lot",99278) X("nobody",99029) X("weapon",98828) X("best",98807) \
	X("station",98720) X("island",98596) X("absolutely",98574) X("instead",98540) X("discussion",98359) X("instead",98231) \
	X("affect",98073) X("design",97967) X("little",97858) X("anyway",97831) X("respond",97750) X("control",97504) X("trouble",97439)\
	X("conversation",97193) X("manage",97135) X("close",97117) X("date",97094) X("public",97031) X("army",97025) X("top",96997) \
	X("post",96926) X("charge",96872) X("seat",96853) X("assume",96840) X("writer",96279) X("perform",96202) X("credit",95981) \
	X("green",95937) X("marriage",95885) X("operation",95878) X("indeed",95860) X("sleep",95600) X("necessary",95580) \
	X("reveal",95401) X("agent",95371) X("access",95371) X("bar",95305) X("debate",95244) X("leg",95185) X("contain",95143) \
	X("beat",94941) X("cool",94920) X("democratic",94862) X("cold",94856) X("glass",94803) X("improve",94764) X("adult",94547) \
	X("trade",94509) X("religious",94485) X("head",94408) X("review",94364) X("kind",94334) X("address",94155) \
	X("association",94100) X("measure",93952) X("stock",93809) X("gas",93763) X("deep",93753) X("lawyer",93526) \
	X("production",93507) X("relate",93433) X("middle",93428) X("management",93289) X("original",93230) X("victim",93207) \
	X("cancer",93157) X("speech",92837) X("particular",92775) X("trial",92762) X("none",92729) X("item",92545) X("weight",92516) \
	X("tomorrow",92490) X("step",92268) X("positive",92223) X("form",92198) X("citizen",92182) X("study",92101) X("trip",91830) \
	X("establish",91776) X("executive",91696) X("politics",91621) X("stick",91597) X("customer",91563) X("manager",91527) \
	X("rather",91475) X("publish",91441) X("popular",91435) X("sing",91395) X("ahead",91325) X("conference",91185) X("total",91071) \
	X("discover",90959) X("fast",90956) X("base",90915) X("direction",90826) X("sunday",90771) X("maintain",90737) X("past",90674) \
	X("majority",90548) X("peace",90518) X("dinner",90516) X("partner",90455) X("user",90342) X("above",90318) X("fly",90236) \
	X("bag",90234) X("therefore",89981) X("rich",89706) X("individual",89686) X("tough",89612) X("owner",89612) X("shall",89582) \
	X("inside",89494) X("voter",89245) X("tool",89236) X("june",89233) X("far",89074) X("may",88994) X("mountain",88984) \
	X("range",88892) X("coach",88826) X("fear",88724) X("friday",88632) X("attorney",88616) X("unless",88605) X("nor",88431) \
	X("expert",88134) X("structure",88114) X("budget",88084) X("insurance",88036) X("text",87811) X("freedom",87806) \
	X("crazy",87804) X("reader",87422) X("style",87244) X("through",87238) X("march",87215) X("machine",87156) X("november",87108) \
	X("generation",87071) X("income",86902) X("born",86829) X("admit",86631) X("hello",86614) X("onto",86605) X("sea",86544) \
	X("okay",86529) X("mouth",86406) X("throughout",86381) X("own",86371) X("test",86155) X("web",85868) X("shake",85841) \
	X("threat",85754) X("solution",85661) X("shut",85625) X("down",85515) X("travel",85441) X("scientist",85428) X("hide",85395) \
	X("obviously",85364) X("refer",85212) X("alone",85036) X("drink",84895) X("investigation",84783) X("senator",84544) \
	X("unit",84518) X("photograph",84476) X("july",84439) X("television",84433) X("key",84228) X("sexual",84220) X("radio",84218) \
	X("prevent",84179) X("once",84165) X("modern",83945) X("senate",83931) X("violence",83882) X("touch",83871) X("feature",83853) \
	X("audience",83493) X("evening",83312) X("whom",83267) X("front",83180) X("hall",82960) X("task",82839) X("score",82738) \
	X("skin",82598) X("suffer",82579) X("wide",82565) X("spring",82557) X("experience",82550) X("civil",82544) X("safety",82535) \
	X("weekend",82429) X("while",82196) X("worth",82178) X("title",82165) X("heat",82112) X("normal",82076) X("hope",81896) \
	X("yard",81852) X("finger",81817) X("tend",81665) X("mission",81524) X("eventually",81481) X("participant",81371) \
	X("hotel",81364) X("judge",81312) X("pattern",81097) X("break",81057) X("institution",80998) X("faith",80912) \
	X("professional",80903) X("reflect",80851) X("folk",80843) X("surface",80819) X("fall",80585) X("client",80584) X("edge",80577) \
	X("traditional",80470) X("council",80469) X("device",80398) X("firm",80364) X("environmental",80328) X("responsibility",80294) \
	X("chair",80186) X("internet",80144) X("october",80099) X("by",80074) X("funny",79978) X("immediately",79856) \
	X("investment",79833) X("ship",79715) X("effective",79635) X("previous",79535) X("content",79496) X("consumer",79489) \
	X("element",79440) X("nuclear",79435) X("spirit",79123) X("directly",79121) X("afraid",78979) X("define",78953) \
	X("handle",78899) X("track",78828) X("run",78645) X("wind",78623) X("lack",78587) X("cost",78520) X("announce",78426) \
	X("journal",78364) X("heavy",78327) X("ice",78316) X("collection",78281) X("feed",78280) X("soldier",78276) X("just",78214) \
	X("governor",78194) X("fish",78114) X("shoulder",78062) X("cultural",78023) X("successful",77999) X("fair",77775) \
	X("trust",77728) X("suddenly",77721) X("future",77663) X("interested",77632) X("deliver",77338) X("saturday",77290) \
	X("editor",77280) X("fresh",77195) X("anybody",77150) X("destroy",77088) X("claim",77050) X("critical",77026) \
	X("agreement",76793) X("powerful",76773) X("researcher",76737) X("concept",76679) X("willing",76629) X("band",76423) \
	X("marry",76412) X("promise",76397) X("easily",76348) X("restaurant",76197) X("league",76178) X("senior",76109) \
	X("capital",76018) X("anymore",75907) X("april",75906) X("potential",75838) X("etc",75726) X("quick",75637) X("magazine",75615) \
	X("status",75577) X("attend",75448) X("replace",75439) X("due",75368) X("hill",75365) X("kitchen",75167) X("achieve",74946) \
	X("screen",74705) X("generally",74681) X("mistake",74643) X("along",74461) X("strike",74378) X("battle",74296) X("spot",74296) \
	X("basic",74208) X("very",74143) X("corner",74047) X("target",73925) X("driver",73902) X("beginning",73853) X("religion",73744) \
	X("crisis",73691) X("count",73650) X("museum",73559) X("engage",73493) X("communication",73413) X("murder",73388) \
	X("blow",73361) X("object",73304) X("express",73274) X("huh",73257) X("encourage",73251) X("matter",73171) X("blog",73163) \
	X("smile",73137) X("return",73109) X("belief",73067) X("block",73065) X("debt",73015) X("fire",72911) X("labor",72759) \
	X("understanding",72651) X("neighborhood",72625) X("contract",72568) X("middle",72563) X("species",72542) X("additional",72539) \
	X("sample",72489) X("involved",72455) X("inside",72435) X("mostly",72378) X("path",72335) X("concerned",72309) X("apple",72273) \
	X("conduct",72181) X("god",72022) X("wonderful",71940) X("library",71921) X("prison",71871) X("hole",71837) X("attempt",71833) \
	X("complete",71752) X("code",71659) X("sales",71601) X("gift",71372) X("refuse",71277) X("increase",71248) X("garden",71243) \
	X("introduce",71221) X("roll",71123) X("christian",71055) X("definitely",70982) X("like",70955) X("lake",70940) X("turn",70831) \
	X("sure",70758) X("earn",70697) X("plane",70679) X("vehicle",70662) X("examine",70501) X("application",70441) \
	X("thousand",70404) X("coffee",70366) X("gain",70275) X("result",70269) X("file",70059) X("billion",70048) X("reform",70010) \
	X("ignore",70007) X("welcome",69914) X("gold",69903) X("jump",69861) X("planet",69822) X("location",69669) X("bird",69662) \
	X("amazing",69525) X("principle",69403) X("promote",69398) X("search",69392) X("nine",69389) X("alive",69360) \
	X("possibility",69316) X("sky",69285) X("otherwise",69243) X("remind",69182) X("healthy",69023) X("fit",68996) X("horse",68883) \
	X("advantage",68866) X("commercial",68821) X("steal",68807) X("basis",68758) X("context",68668) X("highly",68586) \
	X("christmas",68542) X("strength",68537) X("move",68522) X("monday",68488) X("mean",68411) X("alone",68401) X("beach",68303) \
	X("survey",68301) X("writing",68200) X("master",68182) X("cry",68161) X("scale",68139) X("resident",68096) X("football",68073) \
	X("sweet",67903) X("failure",67885) X("reporter",67831) X("commit",67792) X("fight",67767) X("one",67755) X("associate",67750) \
	X("vision",67714) X("function",67710) X("truly",67680) X("sick",67645) X("average",67618) X("human",67605) X("stupid",67597) \
	X("will",67581) X("chinese",67573) X("connection",67550) X("camp",67510) X("stone",67456) X("hundred",67455) X("key",67404) \
	X("truck",67403) X("afternoon",67378) X("responsible",67329) X("secretary",67325) X("apparently",67248) X("smart",67232) \
	X("southern",67179) X("totally",67117) X("western",67085) X("collect",67062) X("conflict",67061) X("burn",66886) \
	X("learning",66879) X("wake",66773) X("contribute",66772) X("ride",66723) X("british",66700) X("following",66684) \
	X("order",66657) X("share",66642) X("newspaper",66598) X("foundation",66578) X("variety",66555) X("perspective",66540) \
	X("document",66467) X("presence",66412) X("stare",66392) X("lesson",66252) X("limit",66220) X("appreciate",66216) \
	X("complete",66198) X("observe",66197) X("currently",66051) X("hundred",66002) X("fun",65982) X("crowd",65965) X("attack",65959)\
	X("apartment",65884) X("survive",65838) X("guest",65818) X("soul",65749) X("protection",65690) X("intelligence",65687) \
	X("yesterday",65635) X("somewhere",65620) X("border",65422) X("reading",65417) X("terms",65413) X("leadership",65396) \
	X("present",65336) X("chief",65311) X("attitude",65288) X("start",65218) X("um",65033) X("deny",64887) X("website",64875) \
	X("seriously",64827) X("actual",64818) X("recall",64789) X("fix",64728) X("negative",64601) X("connect",64462) \
	X("distance",64404) X("regular",64293) X("climate",64223) X("relation",64199) X("flight",64148) X("dangerous",64067) \
	X("boat",64026) X("aspect",63967) X("grab",63946) X("until",63882) X("favorite",63804) X("like",63748) X("january",63714) \
	X("independent",63631) X("volume",63626) X("am",63615) X("lots",63604) X("front",63594) X("online",63564) X("theater",63549) \
	X("speed",63502) X("aware",63488) X("identity",63440) X("demand",63402) X("extra",63392) X("charge",63379) X("guard",63290) \
	X("demonstrate",63269) X("fully",63238) X("tuesday",63179) X("facility",63117) X("farm",62942) X("mind",62869) X("fun",62839) \
	X("thousand",62833) X("august",62821) X("hire",62794) X("light",62687) X("link",62629) X("shoe",62523) X("institute",62451) \
	X("below",62313) X("living",62299) X("european",62156) X("quarter",62138) X("basically",62046) X("forest",61942) \
	X("multiple",61694) X("poll",61677) X("wild",61605) X("measure",61601) X("twice",61563) X("cross",61522) X("background",61431) \
	X("settle",61424) X("winter",61394) X("focus",61392) X("presidential",61351) X("operate",61296) X("fuck",61292) X("view",61111) \
	X("daily",61083) X("shop",61038) X("above",61018) X("division",60892) X("slowly",60889) X("advice",60861) X("reaction",60811) \
	X("injury",60769) X("it",60763) X("ticket",60721) X("grade",60710) X("wow",60702) X("birth",60673) X("painting",60548) \
	X("outcome",60498) X("enemy",60473) X("damage",60384) X("being",60354) X("storm",60280) X("shape",60252) X("bowl",60234) \
	X("commission",60218) X("captain",60187) X("ear",60160) X("troop",60133) X("female",60116) X("wood",60115) X("warm",60062) \
	X("clean",60059) X("lead",59773) X("minister",59752) X("neighbor",59746) X("tiny",59714) X("mental",59701) X("software",59696) \
	X("glad",59681) X("finding",59632) X("lord",59563) X("drive",59524) X("temperature",59493) X("quiet",59485) X("spread",59483) \
	X("bright",59449) X("cut",59434) X("influence",59350) X("kick",59307) X("annual",59301) X("procedure",59288) X("respect",59226) \
	X("wave",59169) X("tradition",59099) X("threaten",59034) X("primary",58990) X("strange",58863) X("actor",58856) X("blame",58815)\
	X("active",58801) X("cat",58763) X("depend",58725) X("bus",58707) X("clothes",58660) X("affair",58553) X("contact",58518) \
	X("category",58470) X("topic",58410) X("victory",58342) X("direct",58289) X("towards",58251) X("map",58218) X("egg",58198) \
	X("ensure",58156) X("general",58129) X("expression",58120) X("past",58119) X("session",58108) X("competition",58102) \
	X("possibly",58071) X("technique",58028) X("mine",58009) X("average",57985) X("intend",57970) X("impossible",57892) \
	X("moral",57787) X("academic",57717) X("wine",57614) X("approach",57607) X("somehow",57596) X("gather",57562) \
	X("scientific",57547) X("african",57497) X("cook",57383) X("participate",57361) X("gay",57195) X("appropriate",57163) \
	X("youth",57132) X("dress",56967) X("straight",56942) X("weather",56939) X("recommend",56938) X("medicine",56814) \
	X("novel",56709) X("obvious",56696) X("thursday",56611) X("exchange",56534) X("explore",56474) X("extend",56458) X("bay",56420) \
	X("invite",56415) X("tie",56394) X("ah",56374) X("belong",56342) X("obtain",56312) X("broad",56310) X("conclusion",56296) \
	X("progress",56162) X("surprise",56116) X("assessment",55956) X("smile",55936) X("feature",55868) X("cash",55856) \
	X("defend",55825) X("pound",55670) X("correct",55632) X("married",55629) X("pair",55530) X("slightly",55389) X("loan",55348) \
	X("village",55217) X("half",55207) X("suit",55200) X("demand",55168) X("historical",55028) X("meaning",55014) X("attempt",54992)\
	X("supply",54988) X("lift",54954) X("ourselves",54951) X("honey",54912) X("bone",54911) X("consequence",54883) X("unique",54827)\
	X("next",54768) X("regulation",54726) X("award",54712) X("bottom",54701) X("excuse",54672) X("familiar",54651) \
	X("classroom",54542) X("search",54487) X("reference",54390) X("emerge",54379) X("long",54354) X("lunch",54300) X("judge",54271) \

static String most_frquent_words[] = {
	#define X(word,occ) {word, sizeof word - 1},
	MOST_FREQUENT_WORDS
	#undef X
};

static isize most_frquent_words_freqs[] = {
	#define X(word,occ) occ,
	MOST_FREQUENT_WORDS
	#undef X
};

#define MOST_FREQ_WORDS_COUNT ARRAY_SIZE(most_frquent_words_freqs)

const isize* most_frquent_words_cumulative_freqs()
{
	static int init = 0;
	static isize data[MOST_FREQ_WORDS_COUNT] = {0};
	if(init == 0)
	{
		init = 1;
		isize accumulator = 0;
		for(isize i = 0; i < MOST_FREQ_WORDS_COUNT; i++)
		{
			accumulator += most_frquent_words_freqs[i];
			data[i] = accumulator; 
		}
	}

	return data;
}

#include <ctype.h>
INTERNAL String_Builder generate_random_text(Allocator* alloc, isize word_count, String separator, bool capitilize, String postfix)
{
	const isize* cumulative_freqs = most_frquent_words_cumulative_freqs();
	isize max_val = cumulative_freqs[MOST_FREQ_WORDS_COUNT - 1];

	String_Builder out = builder_make(alloc, word_count*8 + 5);
	for(isize i = 0; i < word_count; i++)
	{
		isize frquency_guess = random_range(0, max_val);
		isize low = 0;
		isize count = MOST_FREQ_WORDS_COUNT - 1;

		while (count > 0)
		{
			isize step = count / 2;
			isize curr = low + step;
			if (cumulative_freqs[curr] < frquency_guess)
			{
				low = curr + 1;
				count -= step + 1;
			}
			else
				count = step;
		}
		
		ASSERT(0 <= low && low <= max_val);
		ASSERT(frquency_guess <= cumulative_freqs[low]);

		String selected = most_frquent_words[low];

		if(i != 0)
			builder_append(&out, separator);
		builder_append(&out, selected);
	}
	
	if(capitilize && out.len > 0)
		out.data[0] = (char) toupper(out.data[0]);
		
	builder_append(&out, postfix);
	return out;
}
