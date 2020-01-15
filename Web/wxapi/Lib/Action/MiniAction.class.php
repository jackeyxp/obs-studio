<?php
/*************************************************************
    HaoYi (C)2017 - 2018 myhaoyi.com
    备注：专门处理微信小程序请求页面...
*************************************************************/
/**
 * error code 说明.
 * <ul>
 *    <li>-41001: encodingAesKey 非法</li>
 *    <li>-41003: aes 解密失败</li>
 *    <li>-41004: 解密后得到的buffer非法</li>
 *    <li>-41005: base64加密失败</li>
 *    <li>-41016: base64解密失败</li>
 * </ul>
 */
class ErrorCode
{
	public static $OK = 0;
	public static $IllegalAesKey = -41001;
	public static $IllegalIv = -41002;
	public static $IllegalBuffer = -41003;
	public static $DecodeBase64Error = -41004;
}
/**
 * 对微信小程序用户加密数据的解密示例代码.
 *
 * @copyright Copyright (c) 1998-2014 Tencent Inc.
 */
class WXBizDataCrypt
{
  private $appid;
  private $sessionKey;

	/**
	 * 构造函数
	 * @param $sessionKey string 用户在小程序登录后获取的会话密钥
	 * @param $appid string 小程序的appid
	 */
	public function WXBizDataCrypt( $appid, $sessionKey)
	{
		$this->sessionKey = $sessionKey;
		$this->appid = $appid;
	}
	/**
	 * 检验数据的真实性，并且获取解密后的明文.
	 * @param $encryptedData string 加密的用户数据
	 * @param $iv string 与用户数据一同返回的初始向量
	 * @param $data string 解密后的原文
     *
	 * @return int 成功0，失败返回对应的错误码
	 */
	public function decryptData( $encryptedData, $iv, &$data )
	{
		if (strlen($this->sessionKey) != 24) {
			return ErrorCode::$IllegalAesKey;
		}
		$aesKey=base64_decode($this->sessionKey);
		if (strlen($iv) != 24) {
			return ErrorCode::$IllegalIv;
		}
		$aesIV=base64_decode($iv);

		$aesCipher=base64_decode($encryptedData);

		$result=openssl_decrypt( $aesCipher, "AES-128-CBC", $aesKey, 1, $aesIV);

		$dataObj=json_decode( $result );
		if( $dataObj  == NULL )
		{
			return ErrorCode::$IllegalBuffer;
		}
		if( $dataObj->watermark->appid != $this->appid )
		{
			return ErrorCode::$IllegalBuffer;
		}
		$data = $result;
		return ErrorCode::$OK;
	}
}
///////////////////////////////////////////////
// 微信小程序需要用到的数据接口类...
///////////////////////////////////////////////
class MiniAction extends Action
{
  public function _initialize()
  {
    // 支持多个小程序的接入...
    $this->m_weMini = C('HAOYI_MINI');
  }
  //
  // 转发绑定登录状态命令给讲师端|学生端...
  public function bindLogin()
  {
    // 准备返回结果状态...
    $arrErr['err_code'] = false;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['client_type']) || !isset($_POST['bind_cmd']) || !isset($_POST['user_id']) ||
          !isset($_POST['tcp_socket']) || !isset($_POST['tcp_time']) || !isset($_POST['room_id'])) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 读取系统配置数据库记录...
      $dbSys = D('system')->find();
      // 向中心服务器转发小程序绑定登录这个中转命令...
      $dbResult = $this->doTrasmitCmdToServer($dbSys['udpcenter_addr'], $dbSys['udpcenter_port'], kCmd_PHP_Bind_Mini, $_POST);
      // 如果获取连接中转服务器失败...
      if( $dbResult['err_code'] > 0 ) {
        $arrErr['err_code'] = $dbResult['err_code'];
        $arrErr['err_msg'] = $dbResult['err_msg'];
        break;
      }
    } while( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 统一的核心服务器命令接口...
  private function doTrasmitCmdToServer($inServerAddr, $inServerPort, $inCmd, &$dbParam)
  {
    // 通过php扩展插件连接中转服务器 => 性能高...
    $transmit = transmit_connect_server($inServerAddr, $inServerPort);
    // 链接中心服务器失败，直接返回...
    if( !$transmit ) {
      $arrData['err_code'] = true;
      $arrData['err_msg'] = '无法连接中心服务器。';
      return $arrData;
    }
    // 调用php扩展插件的中转接口带上参数...
    $saveJson = json_encode($dbParam);
    $json_data = transmit_command(kClientPHP, $inCmd, $transmit, $saveJson);
    // 关闭中转服务器链接...
    transmit_disconnect_server($transmit);
    // 获取的JSON数据有效，转成数组，直接返回...
    $arrData = json_decode($json_data, true);
    if( !$arrData ) {
      $arrData['err_code'] = true;
      $arrData['err_msg'] = '从中心服务器获取数据失败。';
      return $arrData;
    }
    // 通过错误码，获得错误信息...
    $arrData['err_msg'] = getTransmitErrMsg($arrData['err_code']);
    // 将整个数组返回...
    return $arrData;
  }
  //
  // 终端每隔15秒发起的更新上下行流量统计...
  public function smartFlow()
  {
    // 准备返回结果状态...
    $arrErr['err_code'] = false;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if (!isset($_POST['flow_id']) || !isset($_POST['smart_id'])) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 先查找流量记录，没有直接返回...
      $condition['flow_id'] = $_POST['flow_id'];
      $dbFind = D('FlowView')->where($condition)->find();
      if( !isset($dbFind['flow_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '没有找到流量记录！';
        break;
      }
      // 更新数据库流量记录...
      $dbFlow['updated'] = date('Y-m-d H:i:s');
      $dbFlow['flow_id'] = $_POST['flow_id'];
      $dbFlow['smart_id'] = $_POST['smart_id'];
      $dbFlow['up_flow'] = (isset($_POST['up_flow']) ? $_POST['up_flow'] : 0);
      $dbFlow['down_flow'] = (isset($_POST['down_flow']) ? $_POST['down_flow'] : 0);
      $dbFlow['flow_teacher'] = (isset($_POST['flow_teacher']) ? $_POST['flow_teacher'] : 0);
      // 计算按时间计费的花费 => 计算持续分钟数量 => 向上取整...
      // 注意：终端只要上线之后就开始计费，不管是否推流或拉流...
      $nMinute = ceil((strtotime($dbFlow['updated']) - strtotime($dbFind['created'])) / 60);
      if ($nMinute > 0 && $dbFind['cpm'] > 0 ) {
        $dbFlow['cost'] = $nMinute * $dbFind['cpm'];
        $dbFlow['cpm'] = $dbFind['cpm'];
      }
      // 只存放更大的流量记录 => 避免流量异常时的错误记录...
      $dbFlow['up_flow'] = max($dbFlow['up_flow'], $dbFind['up_flow']);
      $dbFlow['down_flow'] = max($dbFlow['down_flow'], $dbFind['down_flow']);
      D('flow')->save($dbFlow);
    } while( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  // 注意：这是所有终端登录后的第一个命令...
  // 注册Smart主机，返回UDPCenter的TCP地址和端口...
  public function regSmart()
  {
    // 准备返回数据结构...
    $arrErr['err_code'] = false;
    $arrErr['err_msg'] = "OK";
    // 将获得的数据进行判断和处理 => 测试 => $_GET;
    $arrData = $_POST;
    do {
      // 判断输入数据是否有效...
      if( !isset($arrData['mac_addr']) || !isset($arrData['ip_addr']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = "MAC地址或IP地址为空！";
        break;
      }
      // 根据MAC地址获取smart记录信息...
      $mapFind['mac_addr'] = $arrData['mac_addr'];
      $dbSmart = D('smart')->where($mapFind)->find();
      if( count($dbSmart) <= 0 ) {
        // 没有找到记录，直接创建一个新记录...
        $arrData['created'] = date('Y-m-d H:i:s');
        $arrData['updated'] = date('Y-m-d H:i:s');
        $arrErr['smart_id'] = D('smart')->add($arrData);
        // 从数据库中再次获取新增的终端记录...
        //$condition['smart_id'] = $arrErr['smart_id'];
        //$dbSmart = D('smart')->where($condition)->find();
      } else {
        // 找到了记录，直接更新记录...
        $arrData['smart_id'] = $dbSmart['smart_id'];
        $arrData['updated'] = date('Y-m-d H:i:s');
        D('smart')->save($arrData);
        // 准备需要返回的终端编号记录内容...
        $arrErr['smart_id'] = $dbSmart['smart_id'];
      }
      // 获取系统配置信息...
      $dbSys = D('system')->find();
      // 填充UDPCenter的地址和端口...
      $arrErr['udpcenter_addr'] = $dbSys['udpcenter_addr'];
      $arrErr['udpcenter_port'] = $dbSys['udpcenter_port'];
      // 生成与本机相关的唯一本地摄像头和老师摄像头记录...
      $this->doBuildStudentDBCamera($arrErr['smart_id'], 0, "本地摄像头");
      $this->doBuildStudentDBCamera($arrErr['smart_id'], 1, "讲师端画面");
      // 填充所有与本机相关的摄像头列表...
      $condition['smart_id'] = $arrErr['smart_id'];
      $arrErr['camera'] = D('camera')->where($condition)->select();
    } while ( false );
    // 直接反馈查询结果...
    echo json_encode($arrErr);
  }
  // 通过smart_id查找唯一摄像头编号，没有的话创建新记录...
  private function doBuildStudentDBCamera($inSmartID, $inType, $inName)
  {
    $mapFind['camera_type'] = $inType;
    $mapFind['smart_id'] = $inSmartID;
    $dbCamera = D('camera')->where($mapFind)->find();
    if (count($dbCamera) <= 0) {
      $arrData['camera_name'] = $inName;
      $arrData['camera_type'] = $mapFind['camera_type'];
      $arrData['smart_id'] = $mapFind['smart_id'];
      $arrData['created'] = date('Y-m-d H:i:s');
      $arrData['updated'] = date('Y-m-d H:i:s');
      $theDBCameraID = D('camera')->add($arrData);
    } else {
      $theDBCameraID = $dbCamera['camera_id'];
    }
    return $theDBCameraID;
  }
  //
  // 处理终端发起的获取登录用户详情的命令接口...
  public function getLoginUser()
  {
    // 准备返回结果状态...
    $arrErr['err_code'] = false;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效 => user_id|room_id|type_id|smart_id...
      if( !isset($_POST['user_id']) || !isset($_POST['room_id']) || !isset($_POST['type_id']) || !isset($_POST['smart_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 首先，查找用户，如果没有找到，返回错误...
      $condition['user_id'] = $_POST['user_id'];
      $dbUser = D('user')->where($condition)->find();
      if( !isset($dbUser['user_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '无法找到指定的用户记录！';
        break;
      }
      // 保存用户记录到集合当中...
      $arrErr['user'] = $dbUser;
      // 修改房间编号 => 减去房间偏移号码...
      $myQuery['room_id'] = $_POST['room_id'] - LIVE_BEGIN_ID;
      $dbRoom = D('RoomView')->where($myQuery)->find();
      if( !isset($dbRoom['room_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '无法找到指定的房间记录！';
        break;
      }
      // 保存房间记录到集合当中...
      $arrErr['room'] = $dbRoom;
      // 讲师端|学生端都要创建新的流量统计记录...
      $dbFlow['smart_id'] = $_POST['smart_id'];
      $dbFlow['type_id'] = $_POST['type_id'];
      $dbFlow['room_id'] = $dbRoom['room_id'];
      $dbFlow['user_id'] = $dbUser['user_id'];
      $dbFlow['created'] = date('Y-m-d H:i:s');
      $dbFlow['updated'] = date('Y-m-d H:i:s');
      $arrErr['flow_id'] = D('flow')->add($dbFlow);
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 处理来自Student|Teacher|Screen终端的登录房间事件...
  public function loginRoom()
  {
    // 准备返回数据结构...
    $arrErr['err_code'] = false;
    $arrErr['err_msg'] = "OK";
    // 将获得的数据进行判断和处理...$_GET;//
    $arrPost = $_POST;
    do {
      // 判断输入参数是否有效...
      if( !isset($arrPost['room_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '请输入有效的云教室号码，号码从10001开始！';
        break;
      }
      // 获取是否是调试模式的学生端或讲师端对象...
      $bIsDebugMode = ((intval($arrPost['debug_mode']) > 0) ? 1 : 0);
      // 计算有效的的直播间的数据库的编号 => 减去数字前缀偏移...
      $nRoomID = intval($arrPost['room_id']) - LIVE_BEGIN_ID;
      if( $nRoomID <= 0 ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '请输入有效的云教室号码，号码从10001开始！';
        break;
      }
      // 验证云教室是否存在...
      $condition['room_id'] = $nRoomID;
      $dbRoom = D('room')->where($condition)->field('room_id,room_pass')->find();
      if( !isset($dbRoom['room_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '没有找到指定的云教室号码，请确认后重新输入！';
        break;
      }
      // 验证发送的终端类型是否正确...
      $nClientType = intval($arrPost['type_id']);
      if(($nClientType != kClientStudent) && ($nClientType != kClientTeacher) && ($nClientType != kClientScreen)) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '不是合法的终端类型，请确认后重新登录！';
        break;
      }
      // 如果是屏幕终端，需要判断密码是否正确...
      if(($nClientType == kClientScreen) && ($arrPost['room_pass'] != $dbRoom['room_pass'])) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '房间密码错误，请确认后重新登录！';
        break;
      }
      // 读取系统配置数据库记录...
      $dbSys = D('system')->find();
      // 构造UDP中心服务器需要的参数 => 房间编号 => LIVE_BEGIN_ID + room_id
      $dbParam['room_id'] = LIVE_BEGIN_ID + $dbRoom['room_id'];
      $dbParam['debug_mode'] = $bIsDebugMode;
      // 从UDP中心服务器获取UDP直播地址和UDP中转地址...
      $dbResult = $this->doTrasmitCmdToServer($dbSys['udpcenter_addr'], $dbSys['udpcenter_port'], kCmd_PHP_GetUdpServer, $dbParam);
      // 如果获取连接中转服务器失败...
      if( $dbResult['err_code'] > 0 ) {
        $arrErr['err_code'] = $dbResult['err_code'];
        $arrErr['err_msg'] = $dbResult['err_msg'];
        break;
      }
      // 注意：需要将数字转换成字符串...
      // 填充跟踪服务器的地址和端口...
      $arrErr['tracker_addr'] = $dbSys['tracker_addr'];
      $arrErr['tracker_port'] = strval($dbSys['tracker_port']);
      // 填充udp远程服务器的地址和端口 => 从UDPCenter获取的来自UDPServer的汇报...
      $arrErr['remote_addr'] = $dbResult['remote_addr'];
      $arrErr['remote_port'] = strval($dbResult['remote_port']);
      // 填充udp服务器的地址和端口 => 从UDPCenter获取的来自UDPServer的汇报...
      $arrErr['udp_addr'] = $dbResult['udp_addr'];
      $arrErr['udp_port'] = strval($dbResult['udp_port']);
      // 获取当前指定通道上的讲师端和学生端在线数量...
      $arrErr['teacher'] = strval($dbResult['teacher']);
      $arrErr['student'] = strval($dbResult['student']);
      // 注意：房间状态不要通过数据库，直接通过udpserver获取...
    } while( false );
    // 直接反馈最终验证的结果...
    echo json_encode($arrErr);
  }
  //
  // 获取小程序的access_token的值...
  public function getToken($useEcho = true)
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    do {
      // 准备请求需要的url地址...
      $strTokenUrl = sprintf("https://api.weixin.qq.com/cgi-bin/token?grant_type=client_credential&appid=%s&secret=%s",
                           $this->m_weMini['appid'], $this->m_weMini['appsecret']);
      // 直接通过标准API获取access_token...
      $result = http_get($strTokenUrl);
      // 获取access_token失败的情况...
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取access_token失败';
        break;
      }
      // 将获取的数据转换成数组...
      $json = json_decode($result,true);
      if( !$json || isset($json['errcode']) ) {
        $arrErr['err_code'] = $json['errcode'];
        $arrErr['err_msg'] = $json['errmsg'];
        break;
      }
      // 获取access_token成功，返回过期时间和路径信息...
      $arrErr['access_token'] = $json['access_token'];
      $arrErr['expires_in'] = $json['expires_in'];
      $arrErr['mini_path'] = 'pages/bind/bind';
    } while( false );
    // 返回json数据包...
    if ($useEcho) {
      echo json_encode($arrErr);
      return;
    }
    // 返回普通数据...
    return $arrErr;
  }
  //
  // 获取房间列表 => 分页显示...
  public function getRoom()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['cur_page']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 得到每页条数...
      $pagePer = C('PAGE_PER');
      $pageCur = $_POST['cur_page'];  // 当前页码...
      $pageLimit = (($pageCur-1)*$pagePer).','.$pagePer; // 读取范围...
      // 获取记录总数和总页数...
      $totalNum = D('room')->count();
      $max_page = intval($totalNum / $pagePer);
      // 判断是否是整数倍的页码...
      $max_page += (($totalNum % $pagePer) ? 1 : 0);
      // 填充需要返回的信息...
      $arrErr['total_num'] = $totalNum;
      $arrErr['max_page'] = $max_page;
      $arrErr['cur_page'] = $pageCur;
      // 获取房间分页数据，通过视图获取数据...
      $arrRoom = D('RoomView')->limit($pageLimit)->order('Room.created DESC')->select();
      // 这里需要修改房间编号，加上偏移量...
      foreach($arrRoom as &$dbItem) {
        $dbItem['room_id'] = LIVE_BEGIN_ID + $dbItem['room_id'];
      }
      // 组合最终返回的数据结果...
      $arrErr['room'] = $arrRoom;
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 验证房间密码接口...
  public function roomPass()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['room_pass']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 判断房间是否有效 => 房间表中查找...
      $condition['room_pass'] = $_POST['room_pass'];
      $dbRoom = D('RoomView')->where($condition)->find();
      // 密码验证失败，直接返回错误信息...
      if( !isset($dbRoom['room_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '密码错误，请重新输入！';
        break;
      }
      // 验证成功，返回密码对应的房间信息 => 需要修改房间偏移号...
      $dbRoom['room_id'] += LIVE_BEGIN_ID;
      $arrErr['room'] = $dbRoom;
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 获取直播间列表接口...
  public function getLive()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['cur_page']) || !isset($_POST['user_id']) || !isset($_POST['user_type']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 判断是否是超级帐号 => 运营者和管理员...
      $bIsAdmin = (($_POST['user_type'] >= kMaintainUser) ? true : false);
      $condition['master_id'] = $_POST['user_id'];
      // 得到每页条数...
      $pagePer = C('PAGE_PER');
      $pageCur = $_POST['cur_page'];  // 当前页码...
      $pageLimit = (($pageCur-1)*$pagePer).','.$pagePer; // 读取范围...
      // 获取记录总数和总页数...
      if( $bIsAdmin ) {
        $totalNum = D('room')->count();
      } else {
        $totalNum = D('LiveView')->where($condition)->count();
      }
      $max_page = intval($totalNum / $pagePer);
      // 判断是否是整数倍的页码...
      $max_page += (($totalNum % $pagePer) ? 1 : 0);
      // 填充需要返回的信息...
      $arrErr['total_num'] = $totalNum;
      $arrErr['max_page'] = $max_page;
      $arrErr['cur_page'] = $pageCur;
      // 获取机构分页数据，直接通过数据表读取...
      if( $bIsAdmin ) {
        $arrLive = D('LiveView')->limit($pageLimit)->order('Room.created DESC')->select();
      } else {
        $arrLive = D('LiveView')->where($condition)->limit($pageLimit)->order('Room.created DESC')->select();
      }
      // 修改直播间编号 => 终端软件使用...
      foreach($arrLive as &$dbItem) {
        $dbItem['room_id'] = LIVE_BEGIN_ID + $dbItem['room_id'];
      }
      // 组合最终返回的数据结果...
      $arrErr['live'] = $arrLive;
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 获取直播间扩展信息...
  public function getExLive()
  {
    $arrErr['subject'] = D('subject')->field('subject_id,subject_name')->select();
    $arrErr['agent'] = D('agent')->field('agent_id,name')->select();
    echo json_encode($arrErr);
  }
  //
  // 删除直播间记录的接口...
  public function delLive()
  {
    // 修改房间编号 => 减去房间偏移号码...
    $_POST['room_id'] = $_POST['room_id'] - LIVE_BEGIN_ID;
    $condition['room_id'] = $_POST['room_id'];
    D('room')->where($condition)->delete();
    unlink($_POST['qrcode']);
  }
  //
  // 更新直播间记录的接口...
  public function saveLive()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    do {
      if( !isset($_POST['room_pass']) || !isset($_POST['room_id']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入的参数无效';
        break;
      }
      // 修改房间编号 => 减去房间偏移号码...
      $_POST['room_id'] = $_POST['room_id'] - LIVE_BEGIN_ID;
      // 判断输入密码的唯一性...
      $condition['room_pass'] = $_POST['room_pass'];
      $condition['room_id'] = array('neq', $_POST['room_id']);
      $arrFind = D('room')->where($condition)->field('room_id,room_pass')->select();
      if( count($arrFind) > 0 ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入的密码已经存在，请重新输入！';
        break;
      }
      // 将输入的数据存入数据库...
      D('room')->save($_POST);
    } while ( false );
    // 返回json数据包...
    echo json_encode($arrErr);
  }
  
  // 专门生成直播间二维码测试代码...
  /*public function doTest()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    do {
      // 获取小程序码的token值...
      $arrToken = $this->getToken(false);
      if ($arrToken['err_code'] > 0) {
        $arrErr['err_code'] = $arrToken['err_code'];
        $arrErr['err_msg'] = $arrToken['err_msg'];
        break;
      }
      // 通过小程序码再获取小程序二维码，然后存入数据库...
      $strQRUrl = sprintf("https://api.weixin.qq.com/wxa/getwxacodeunlimit?access_token=%s", $arrToken['access_token']);
      $itemData["scene"] = "room_" . $_GET['room_id'];
      $itemData["page"] = "pages/home/home";
      $itemData["width"] = 280;
      // 直接通过设定参数获取小程序码...
      $result = http_post($strQRUrl, json_encode($itemData));
      // 获取小程序码失败的情况...
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取小程序码失败';
        break;
      }
      // 将小程序码写入指定的上传目录当中，并将图片地址写入数据库...
      $strImgPath = sprintf("%s/upload/%s.jpg", APP_PATH, $itemData["scene"]);
      file_put_contents($strImgPath, $result);
    } while ( false );
    echo json_encode($arrErr);
  }*/
  
  //
  // 新建直播间记录的接口...
  public function addLive()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    do {
      if( !isset($_POST['room_pass']) || !isset($_POST['room_name']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入的参数无效';
        break;
      }
      // 判断输入密码的唯一性...
      $condition['room_pass'] = $_POST['room_pass'];
      $arrFind = D('room')->where($condition)->field('room_id,room_pass')->select();
      if( count($arrFind) > 0 ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入的密码已经存在，请重新输入！';
        break;
      }
      $dbLive = $_POST;
      $dbLive['created'] = date('Y-m-d H:i:s');
      $dbLive['updated'] = date('Y-m-d H:i:s');
      $dbLive['room_id'] = D('room')->add($dbLive);
      // 将新创建的数据库记录返回给小程序...
      $arrErr['live'] = $dbLive;
      // 获取小程序码的token值...
      $arrToken = $this->getToken(false);
      if ($arrToken['err_code'] > 0) {
        $arrErr['err_code'] = $arrToken['err_code'];
        $arrErr['err_msg'] = $arrToken['err_msg'];
        break;
      }
      // 通过小程序码再获取小程序二维码，然后存入数据库...
      $strQRUrl = sprintf("https://api.weixin.qq.com/wxa/getwxacodeunlimit?access_token=%s", $arrToken['access_token']);
      $itemData["scene"] = "room_" . $dbLive['room_id'];
      $itemData["page"] = "pages/home/home";
      $itemData["width"] = 280;
      // 直接通过设定参数获取小程序码...
      $result = http_post($strQRUrl, json_encode($itemData));
      // 获取小程序码失败的情况...
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取小程序码失败';
        break;
      }
      // 将小程序码写入指定的上传目录当中，并将图片地址写入数据库...
      $strImgPath = sprintf("%s/upload/%s.jpg", APP_PATH, $itemData["scene"]);
      file_put_contents($strImgPath, $result);
      $dbLive['qrcode'] = $strImgPath;
      D('room')->save($dbLive);
      // 修改房间编号，增加房间偏移号码...
      $dbLive['room_id'] = $dbLive['room_id'] + LIVE_BEGIN_ID;
      $arrErr['live'] = $dbLive;
    } while ( false );
    // 返回json数据包...
    echo json_encode($arrErr);
  }
  //
  // 处理小程序登录事件...
  public function login()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入参数的有效性 => 没有设置或数据为空，返回错误 => 去掉 miniName 字段...
      if( !isset($_POST['code']) || !isset($_POST['encrypt']) || !isset($_POST['iv']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入的参数无效';
        break;
      }
      // 准备请求需要的url地址 => jscode2session => 用code换取session_key...
      $strUrl = sprintf("https://api.weixin.qq.com/sns/jscode2session?appid=%s&secret=%s&js_code=%s&grant_type=authorization_code",
                         $this->m_weMini['appid'], $this->m_weMini['appsecret'], $_POST['code']);
      // code 换取 openid | session_key，判断返回结果...
      $result = http_get($strUrl);
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取openid失败';
        break;
      }
      // 解析微信API返回数据，发生错误...
      $json = json_decode($result,true);
      if( !$json || isset($json['errcode']) ) {
        $arrErr['err_code'] = $json['errcode'];
        $arrErr['err_msg'] = $json['errmsg'];
        break;
      }
      // 获取到了正确的 openid | session_key，构造解密对象...
      $wxCrypt = new WXBizDataCrypt($this->m_weMini['appid'], $json['session_key']);
      $theErr = $wxCrypt->decryptData($_POST['encrypt'], $_POST['iv'], $outData);
      // 解码失败，返回错误...
      if( $theErr != 0 ) {
        $arrErr['err_code'] = $theErr;
        $arrErr['err_msg'] = '数据解密失败';
        break;
      }
      // 将获取的数据转换成数组 => 有些字段包含大写字母...
      $arrUser = json_decode($outData, true);
      if( !isset($arrUser['unionId']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '没有获取到unionid';
        break;
      }
      // 微信昵称中去除emoji表情符号的操作...
      $arrUser['nickName'] = trimEmo($arrUser['nickName']);
      // 将获取到的用户关键值查找数据库内容...
      // 这里是小程序，注意有些字段有大写字母，字段标识也有区别...
      $where['wx_unionid'] = $arrUser['unionId'];
      $dbUser = D('UserMast')->where($where)->find();
      // 根据小程序名称设置openid...
      $dbUser['wx_cloud_mini'] = $arrUser['openId'];
      // 从微信获取的信息更新到数据库当中...
      // 这里是小程序，注意有些字段有大写字母，字段标识也有区别...
      $dbUser['wx_unionid'] = $arrUser['unionId'];    // 全局唯一ID
      $dbUser['wx_nickname'] = $arrUser['nickName'];  // 微信昵称
      $dbUser['wx_language'] = $arrUser['language'];  // 语言
      $dbUser['wx_headurl'] = $arrUser['avatarUrl'];  // 0,46,64,96,132
      $dbUser['wx_country'] = $arrUser['country'];    // 国家
      $dbUser['wx_province'] = $arrUser['province'];  // 省份
      $dbUser['wx_city'] = $arrUser['city'];          // 城市
      $dbUser['wx_sex'] = $arrUser['gender'];         // 性别
      // 更新 $_POST 传递过来的其它数据到数据库对象当中 => 设置了字段才更新保存...
      if( isset($_POST['wx_brand']) ) { $dbUser['wx_brand'] = $_POST['wx_brand']; }
      if( isset($_POST['wx_model']) ) { $dbUser['wx_model'] = $_POST['wx_model']; }
      if( isset($_POST['wx_version']) ) { $dbUser['wx_version'] = $_POST['wx_version']; }
      if( isset($_POST['wx_system']) ) { $dbUser['wx_system'] = $_POST['wx_system']; }
      if( isset($_POST['wx_platform']) ) { $dbUser['wx_platform'] = $_POST['wx_platform']; }
      if( isset($_POST['wx_SDKVersion']) ) { $dbUser['wx_SDKVersion'] = $_POST['wx_SDKVersion']; }
      if( isset($_POST['wx_pixelRatio']) ) { $dbUser['wx_pixelRatio'] = $_POST['wx_pixelRatio']; }
      if( isset($_POST['wx_screenWidth']) ) { $dbUser['wx_screenWidth'] = $_POST['wx_screenWidth']; }
      if( isset($_POST['wx_screenHeight']) ) { $dbUser['wx_screenHeight'] = $_POST['wx_screenHeight']; }
      if( isset($_POST['wx_fontSizeSetting']) ) { $dbUser['wx_fontSizeSetting'] = $_POST['wx_fontSizeSetting']; }
      // 根据id字段判断是否有记录...
      if( isset($dbUser['user_id']) ) {
        // 更新已有的用户记录...
        $dbUser['updated'] = date('Y-m-d H:i:s');
        $condition['user_id'] = $dbUser['user_id'];
        D('user')->where($condition)->save($dbUser);
      } else {
        // 新建一条用户记录...
        $dbUser['created'] = date('Y-m-d H:i:s');
        $dbUser['updated'] = date('Y-m-d H:i:s');
        $insertid = D('user')->add($dbUser);
        $dbUser['user_id'] = $insertid;
        // 设定默认的用户类型|真实姓名...
        $dbUser['user_type'] = kParentUser;
      }
      // 返回得到的用户编号|用户类型|真实姓名...
      $arrErr['user_id'] = $dbUser['user_id'];
      $arrErr['user_type'] = $dbUser['user_type'];
      $arrErr['real_name'] = $dbUser['real_name'];
      $arrErr['master_shop_id'] = (isset($dbUser['master_shop_id']) ? $dbUser['master_shop_id'] : 0);
      $arrErr['master_agent_id'] = (isset($dbUser['master_agent_id']) ? $dbUser['master_agent_id'] : 0);
     }while( false );
    // 返回json数据包...
    echo json_encode($arrErr);
  }
  //
  // 获取机构列表接口...
  public function getAgent()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['cur_page']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 得到每页条数...
      $pagePer = C('PAGE_PER');
      $pageCur = $_POST['cur_page'];  // 当前页码...
      $pageLimit = (($pageCur-1)*$pagePer).','.$pagePer; // 读取范围...
      // 获取记录总数和总页数...
      $totalNum = D('agent')->count();
      $max_page = intval($totalNum / $pagePer);
      // 判断是否是整数倍的页码...
      $max_page += (($totalNum % $pagePer) ? 1 : 0);
      // 填充需要返回的信息...
      $arrErr['total_num'] = $totalNum;
      $arrErr['max_page'] = $max_page;
      $arrErr['cur_page'] = $pageCur;
      // 获取机构分页数据，直接通过数据表读取...
      $arrErr['agent'] = D('AgentView')->limit($pageLimit)->order('created DESC')->select();
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 删除机构记录的接口...
  public function delAgent()
  {
    $condition['agent_id'] = $_POST['agent_id'];
    D('agent')->where($condition)->delete();
    unlink($_POST['qrcode']);
  }
  //
  // 更新机构记录的接口...
  public function saveAgent()
  {
    D('agent')->save($_POST);
  }
  //
  // 新建机构记录的接口...
  public function addAgent()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 保存到临时对象...
    $dbAgent = $_POST;
    $dbAgent['created'] = date('Y-m-d H:i:s');
    $dbAgent['agent_id'] = D('agent')->add($dbAgent);
    // 将新创建的数据库记录返回给小程序...
    $arrErr['agent'] = $dbAgent;
    do {
      // 获取小程序码的token值...
      $arrToken = $this->getToken(false);
      if ($arrToken['err_code'] > 0) {
        $arrErr['err_code'] = $arrToken['err_code'];
        $arrErr['err_msg'] = $arrToken['err_msg'];
        break;
      }
      // 通过小程序码再获取小程序二维码，然后存入数据库...
      $strQRUrl = sprintf("https://api.weixin.qq.com/wxa/getwxacodeunlimit?access_token=%s", $arrToken['access_token']);
      $itemData["scene"] = "agent_" . $dbAgent['agent_id'];
      $itemData["page"] = "pages/home/home";
      $itemData["width"] = 280;
      // 直接通过设定参数获取小程序码...
      $result = http_post($strQRUrl, json_encode($itemData));
      // 获取小程序码失败的情况...
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取小程序码失败';
        break;
      }
      // 将小程序码写入指定的上传目录当中，并将图片地址写入数据库...
      $strImgPath = sprintf("%s/upload/%s.jpg", APP_PATH, $itemData["scene"]);
      file_put_contents($strImgPath, $result);
      $dbAgent['qrcode'] = $strImgPath;
      $arrErr['agent'] = $dbAgent;
      D('agent')->save($dbAgent);
    } while( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  
  public function getAgentFree()
  {
    // 找到所有 user_type > kTeacherUser 的用户，这些用户都可以成为机构管理员...
    $condition['user_type'] = array('gt', kTeacherUser);
    $arrMaster = D('user')->where($condition)->field('user_id,wx_nickname')->select();
    echo json_encode($arrMaster);
  }
  //
  // 得到所有的可用的校长列表 => 没有被占用的自由校长列表...
  public function getMasterFree()
  {
    $curMasterID = (isset($_GET['master_id']) ? intval($_GET['master_id']) : 0);
    $arrErr['master'] = $this->findMasterFree($curMasterID);
    $arrErr['agent'] = D('agent')->field('agent_id,name')->select();
    echo json_encode($arrErr);
  }
  //
  // 查找空闲的校长列表...
  private function findMasterFree($inCurMasterID)
  {
    // 找到所有 user_type > kTeacherUser 的用户，这些用户都可以成为校长...
    $condition['user_type'] = array('gt', kTeacherUser);
    $arrMaster = D('user')->where($condition)->field('user_id,wx_nickname')->select();
    // 找到所有有校长的学校列表，然后排除对应的用户...
    $arrShop = D('shop')->where('master_id > 0')->field('shop_id,master_id')->select();
    // 找到没有被占用的自由校长列表...
    $arrFree = array();
		foreach($arrMaster as &$dbItem) {
      $bFreeFlag = true;
      if( $inCurMasterID != $dbItem['user_id'] ) {
        foreach($arrShop as &$dbShop) {
          if( $dbItem['user_id'] == $dbShop['master_id'] ) {
            $bFreeFlag = false;
            break;
          }
        }
      }
      // 如果在学校中没有找到对应的校长，则是自由校长...
      if( $bFreeFlag ) {
        array_push($arrFree, $dbItem);
      }
    }
    // 返回找到的数组...
    return $arrFree;
  }  
  //
  // 获取所有学校|门店列表接口...
  public function getAllShop()
  {
    $arrShop = D('shop')->field('shop_id,name')->order('created ASC')->select();
    echo json_encode($arrShop);
  }
  //
  // 获取学校|门店列表接口...
  public function getShop()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['cur_page']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 得到每页条数...
      $pagePer = C('PAGE_PER');
      $pageCur = $_POST['cur_page'];  // 当前页码...
      $pageLimit = (($pageCur-1)*$pagePer).','.$pagePer; // 读取范围...
      // 获取记录总数和总页数...
      $totalNum = D('shop')->count();
      $max_page = intval($totalNum / $pagePer);
      // 判断是否是整数倍的页码...
      $max_page += (($totalNum % $pagePer) ? 1 : 0);
      // 填充需要返回的信息...
      $arrErr['total_num'] = $totalNum;
      $arrErr['max_page'] = $max_page;
      $arrErr['cur_page'] = $pageCur;
      // 获取学校|门店分页数据，通过视图获取数据，并查找所有空闲的管理员列表...
      $arrErr['shop'] = D('ShopView')->limit($pageLimit)->order('Shop.created DESC')->select();
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 删除学校|门店记录的接口...
  public function delShop()
  {
    $condition['shop_id'] = $_POST['shop_id'];
    D('shop')->where($condition)->delete();
    unlink($_POST['qrcode']);
  }
  //
  // 更新学校|门店记录的接口...
  public function saveShop()
  {
    D('shop')->save($_POST);
  }
  //
  // 新建学校|门店记录的接口...
  public function addShop()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 保存到临时对象...
    $dbShop = $_POST;
    $dbShop['created'] = date('Y-m-d H:i:s');
    $dbShop['shop_id'] = D('shop')->add($dbShop);
    // 将新创建的数据库记录返回给小程序...
    $arrErr['shop'] = $dbShop;
    do {
      // 获取小程序码的token值...
      $arrToken = $this->getToken(false);
      if ($arrToken['err_code'] > 0) {
        $arrErr['err_code'] = $arrToken['err_code'];
        $arrErr['err_msg'] = $arrToken['err_msg'];
        break;
      }
      // 通过小程序码再获取小程序二维码，然后存入数据库...
      $strQRUrl = sprintf("https://api.weixin.qq.com/wxa/getwxacodeunlimit?access_token=%s", $arrToken['access_token']);
      $itemData["scene"] = "shop_" . $dbShop['shop_id'];
      $itemData["page"] = "pages/home/home";
      $itemData["width"] = 280;
      // 直接通过设定参数获取小程序码...
      $result = http_post($strQRUrl, json_encode($itemData));
      // 获取小程序码失败的情况...
      if( !$result ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '获取小程序码失败';
        break;
      }
      // 将小程序码写入指定的上传目录当中，并将图片地址写入数据库...
      $strImgPath = sprintf("%s/upload/%s.jpg", APP_PATH, $itemData["scene"]);
      file_put_contents($strImgPath, $result);
      $dbShop['qrcode'] = $strImgPath;
      $arrErr['shop'] = $dbShop;
      D('shop')->save($dbShop);
    } while( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 获取会员用户列表接口...
  public function getUser()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 注意：这里使用的是 $_POST 数据...
    do {
      // 判断输入的参数是否有效...
      if( !isset($_POST['cur_page']) ) {
        $arrErr['err_code'] = true;
        $arrErr['err_msg'] = '输入参数无效！';
        break;
      }
      // 获取查询内容，并设定是否是查询状态...
      $strSearch = (isset($_POST['search']) ? $_POST['search'] : '');
      $bIsSearch = (strlen($strSearch) > 0 ? true : false);
      // 得到每页条数...
      $pagePer = C('PAGE_PER');
      $pageCur = $_POST['cur_page'];  // 当前页码...
      $pageLimit = (($pageCur-1)*$pagePer).','.$pagePer; // 读取范围...
      // 如果是查询状态，需要配置查询参数...
      if ( $bIsSearch ) {
        $arrWhere['_string']  = "(wx_nickname like '%$strSearch%') OR (real_name like '%$strSearch%') OR ";
        $arrWhere['_string'] .= "(child_name like '%$strSearch%') OR (child_nick like '%$strSearch%')";
        $totalNum = D('UserView')->where($arrWhere)->count();
      } else {
        $totalNum = D('UserView')->count();
      }
      $max_page = intval($totalNum / $pagePer);
      // 判断是否是整数倍的页码...
      $max_page += (($totalNum % $pagePer) ? 1 : 0);
      // 填充需要返回的信息...
      $arrErr['total_num'] = $totalNum;
      $arrErr['max_page'] = $max_page;
      $arrErr['cur_page'] = $pageCur;
      // 获取会员用户分页数据，通过视图获取数据...
      if ( $bIsSearch ) {
        $arrErr['user'] = D('UserView')->where($arrWhere)->limit($pageLimit)->order('User.updated DESC, User.created DESC')->select();
      } else {
        $arrErr['user'] = D('UserView')->limit($pageLimit)->order('User.updated DESC, User.created DESC')->select();
      }
    } while ( false );
    // 返回json编码数据包...
    echo json_encode($arrErr);
  }
  //
  // 更新用户记录的接口...
  public function saveUser()
  {
    D('user')->save($_POST);
  }
  //
  // 更新宝宝信息的接口...
  public function saveChild()
  {
    // 准备返回信息...
    $arrErr['err_code'] = 0;
    $arrErr['err_msg'] = 'ok';
    // 保存到临时对象...
    $dbChild = $_POST;
    if(isset($dbChild['child_id']) && $dbChild['child_id'] > 0) {
      $dbChild['updated'] = date('Y-m-d H:i:s');
      D('child')->save($dbChild);
    } else {
      unset($dbChild['child_id']);
      $dbChild['created'] = date('Y-m-d H:i:s');
      $dbChild['updated'] = date('Y-m-d H:i:s');
      $dbChild['child_id'] = D('child')->add($dbChild);
      $dbUser['user_id'] = $dbChild['user_id'];
      $dbUser['child_id'] = $dbChild['child_id'];
      D('user')->save($dbUser);
    }
    // 返回child_id编号，json编码数据包...
    $arrErr['child_id'] = $dbChild['child_id'];
    echo json_encode($arrErr);
  }
}
?>