import os;
os.chdir(os.path.dirname(__file__));

import pycurl;
import base64;
import io, json;
import datetime;

from typing import List, Dict, Tuple, Optional, Any;

json_t = Dict[str, Any];
download_folder : str = "../downloads";
replay_folder : str = os.path.join(download_folder, "replay");

def strs_in_strs(target : "str | List[str]", lib : "str | List[str]") -> bool:
    if isinstance(target, str):
        target = (target,);
    if isinstance(lib, str):
        lib = (lib,);

    for _t in target:
        for _l in lib:
            if _t in _l:
                return True;
    return False;

class Fetcher:
    """资源下载器, 用于从saiblo网站上获取信息"""
    auth_key : str;

    def __init__(self, _auth_key : str) -> None:
        """初始化下载器

        Args:
            _auth_key (str): 提供给服务器的token, 可以在排行榜处观察发送的http请求的authorization字段而获得
        """
        self.auth_key = _auth_key.replace("Bearer ", "");

    def fetch_match(self, player : "str | Tuple[str, str]", max_count : int = 1e4,
    start_time : Optional[datetime.datetime] = None, end_time : Optional[datetime.datetime] = None) -> None:
        """下载一段时间内的与给定玩家相关的对局, 允许利用对局发起时间、最大下载数量等进一步限制范围

        支持同时指定多个限制条件以进行复合筛选

        状态不是"评测成功"的对局会被自动跳过

        Args:
            player (str | Tuple[str, str]): 给定的玩家, 传入tuple表示下载这两个玩家间的对局
            max_count (int, optional): 最大下载的对局数. 默认无限制.
            start_time (datetime.datetime, optional): 给定的起始时间, 晚于该时间发起的对局才会被下载. 默认无限制.
            end_time (datetime.datetime, optional): 给定的结束时间, 早于该时间发起的对局才会被下载. 默认无限制.
        """
        print("Fetching starts at " + str(datetime.datetime.now()));

        search_player : str = player if isinstance(player, str) else player[0];
        another_player : str = "" if isinstance(player, str) else player[1];

        query_step : int = min(20, round(max_count * 1.2));
        query_offset : int = 0;
        download_count : int = 0;
        while True:
            response = self.__do_query(query_step, query_offset, search_player);
            results : List[json_t]= response["results"];

            ending : bool = False;
            for match in results:
                match_time = datetime.datetime.strptime(match["create_time"][:-6], '%a, %d %b %Y %H:%M:%S');
                if (start_time is not None) and match_time < start_time:
                    ending = True;
                    break;
                if (end_time is not None) and match_time > end_time:
                    continue;
                if (None in tuple(map(lambda x: x["code"], match["info"]))) or (match["state"] != "评测成功"):
                    continue;

                match_players : Tuple[str] = tuple(map(lambda x: x["user"]["username"], match["info"]));
                match_ais : Tuple[str] = tuple(map(lambda x: "%s %s" % (x["code"]["entity"], x["code"]["version"]), match["info"]));

                if another_player and not strs_in_strs(another_player, match_players):
                    continue;

                if not ("%s.json" % match["id"]) in os.listdir(replay_folder):
                    self.__download_replay(match["id"], match);
                download_count += 1;

                if download_count >= max_count:
                    ending = True;
                    break;

            if ending or len(results) < query_step:
                break;
            query_offset += query_step;

    def __call_pycurl(self, url : str, extra_header : List[str] = []) -> str:
        headers = [
            "authority: api.saiblo.net", "accept: application/json, text/plain, */*", "accept-language: zh-CN,zh;q=0.9",
            "authorization: Bearer %s" % self.auth_key, "origin: https://www.saiblo.net", "referer: https://www.saiblo.net/"
        ];
        headers.extend(extra_header);

        buffer = io.BytesIO();
        c = pycurl.Curl();
        c.setopt(c.URL, url);
        c.setopt(c.HTTPHEADER, headers);
        c.setopt(c.WRITEDATA, buffer);
        c.perform();
        c.close();

        return buffer.getvalue().decode("utf-8");

    def __do_query(self, limit : int, offset : int, username : str) -> json_t:
        print("Query: [limit: %d, offset: %d, player: %s]" % (limit, offset, username));
        return json.loads(self.__call_pycurl("https://api.saiblo.net/api/matches/?limit=%d&offset=%d&username=%s" % (limit, offset, username)));
    def __download_replay(self, match_id : "int | str", upper_level_message : json_t) -> None:
        if isinstance(match_id, int):
            match_id = str(match_id);
        print("Download: %s" % match_id);

        replay_json = {"replay" : json.loads(self.__call_pycurl("https://api.saiblo.net/api/matches/%s/download/" % match_id))};
        for _name in ("id", "create_time", "info", "message"):
            replay_json[_name] = upper_level_message[_name];

        with open("../downloads/replay/%s.json" % match_id, "w") as f:
            json.dump(replay_json, f);

class Analyzer:
    """对局分析工具, 目前用于筛选满足指定条件的对局"""
    @staticmethod
    def get_files() -> List[str]:
        return list(map(lambda x: os.path.join(replay_folder, x), os.listdir(replay_folder)));
    @staticmethod
    def clear_replay() -> None:
        """清除已下载的所有回放"""
        tuple(map(lambda file: os.remove(file), Analyzer.get_files()));
    @staticmethod
    def find_breakthrough(player : str, delete : int = 0) -> None:
        """在已下载的回放中寻找指定玩家“扣了血”的对局

        Args:
            player (str): 关注的玩家名
            delete (int, optional): 删除选项,1表示删除选中项,-1表示删除未选中项. 默认不删除.
        """
        for match_file in Analyzer.get_files():
            match_json : json_t = json.load(open(match_file));
            last_hp : Tuple[int, int] = match_json["replay"][-1]["round_state"]["camps"]

            matching : bool = False;
            for ind, thing in enumerate(match_json["info"]):
                if (thing is None) or (thing["user"]["username"] != player):
                    continue;
                if last_hp[ind] < 50:
                    matching = True;
                    print("Match %d: hp%d %s at %s" % (thing["match"], last_hp[ind],
                        tuple(map(lambda x: "%s %s" % (x["user"]["username"], x["code"]["entity"]), match_json["info"])), match_json["create_time"]));
            if (not matching and delete == -1) or (matching and delete == 1):
                os.remove(match_file);
    @staticmethod
    def scan_result(player : str, ai_name : str = None) -> None:
        for match_file in Analyzer.get_files():
            match_json : json_t = json.load(open(match_file));

            for ind, thing in enumerate(match_json["info"]):
                if (thing is None) or (thing["user"]["username"] != player):
                    continue;
                if ai_name and not strs_in_strs(ai_name, thing["code"]["entity"]):
                    continue;

                msg_json = json.loads(match_json["message"]);
                msg = base64.b64decode(msg_json["states"][ind]["stderr"]).decode().splitlines();
                last_msg : str = "";
                for i in msg:
                    if i.find("511 HP:") >= 0:
                        last_msg = i.replace("511 ", "");

                print("Match %d posi%d : %s [%s] at %s" % (thing["match"], ind,
                    tuple(map(lambda x: "%s %s" % (x["user"]["username"], x["code"]["entity"]), match_json["info"])), last_msg, match_json["create_time"]));