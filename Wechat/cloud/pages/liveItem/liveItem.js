
import Notify from '../../vant-weapp/notify/notify';
import Dialog from '../../vant-weapp/dialog/dialog';

// 获取全局的app对象...
const g_appData = getApp().globalData;

Page({
  /**
   * 页面的初始数据
   */
  data: {
    m_bEdit: false,
    m_curAgentName: '',
    m_curSubjectName: '',
    m_curAgentID: 0,
    m_curSubjectID: 0,
    m_arrAgent: [],
    m_arrSubject: [],
    m_liveName: '',
    m_livePass: '',
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    // 设置界面是否是系统管理员身份 => 可以添加|删除...
    let theIsAdmin = ((g_appData.m_userInfo.userType >= g_appData.m_userTypeID.kMaintainUser) ? true : false);
    this.setData({ m_isAdmin: theIsAdmin });
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let isEdit = parseInt(options.edit);
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/getExLive';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'GET',
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('获取直播间扩展信息失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        let arrJson = JSON.parse(res.data);
        that.doShowLive(isEdit, arrJson.subject, arrJson.agent);
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        Notify('获取直播间扩展信息失败！');
      }
    });
  },
  // 显示具体的直播间操作界面...
  doShowLive: function (isEdit, arrSubject, arrAgent) {
    let strTitle = (isEdit ? '修改' : '添加') + ' - 直播间';
    let theLive = isEdit ? g_appData.m_curSelectItem : null;
    let theLiveName = isEdit ? theLive.room_name : '';
    let theLivePass = isEdit ? theLive.room_pass : '';
    theLivePass = ((typeof theLivePass === 'undefined') ? '' : theLivePass);
    let theCurSubjectID = (isEdit ? theLive.subject_id : 0);
    let theCurSubjectName = isEdit ? theLive.subject_name : '请选择';
    let theCurAgentID = (isEdit ? theLive.agent_id : 0);
    let theCurAgentName = isEdit ? theLive.agent_name : '请选择';
    let theCurSubjectIndex = 0; let theCurAgentIndex = 0;
    // 修改标题信息...
    wx.setNavigationBarTitle({ title: strTitle });
    // 如果是编辑状态，需要找到科目所在的索引编号...
    if (isEdit && arrSubject.length > 0) {
      for (let index = 0; index < arrSubject.length; ++index) {
        if (arrSubject[index].subject_id == theCurSubjectID) {
          theCurSubjectIndex = index;
          break;
        }
      }
    }
    // 如果是编辑状态，需要找到机构所在的索引编号...
    if (isEdit && arrAgent.length > 0) {
      for (let index = 0; index < arrAgent.length; ++index) {
        if (arrAgent[index].agent_id == theCurAgentID) {
          theCurAgentIndex = index;
          break;
        }
      }
    }
    // 应用到界面...
    this.setData({
      m_bEdit: isEdit,
      m_liveName: theLiveName,
      m_livePass: theLivePass,
      m_arrSubject: arrSubject,
      m_curSubjectID: theCurSubjectID,
      m_curSubjectName: theCurSubjectName,
      m_curSubjectIndex: theCurSubjectIndex,
      m_arrAgent: arrAgent,
      m_curAgentID: theCurAgentID,
      m_curAgentName: theCurAgentName,
      m_curAgentIndex: theCurAgentIndex,
    });
  },
  // 点击取消按钮...
  doBtnCancel: function (event) {
    wx.navigateBack();
  },
  // 点击保存按钮...
  doBtnSave: function (event) {
    if (this.data.m_liveName.length <= 0) {
      Notify('【房间名称】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_livePass.length != 6) {
      Notify('【房间密码】必须是6位数字，请重新输入！');
      return;
    }
    if (parseInt(this.data.m_curSubjectID) <= 0) {
      Notify('【所属科目】不能为空，请重新选择！');
      return;
    }
    if (parseInt(this.data.m_curAgentID) <= 0) {
      Notify('【所属机构】不能为空，请重新选择！');
      return;
    }
    // 根据不同的标志进行不同的接口调用...
    this.data.m_bEdit ? this.doLiveSave() : this.doLiveAdd();
  },
  // 进行直播间的添加操作...
  doLiveAdd: function () {
    wx.showLoading({ title: '加载中' });
    let that = this;
    // 准备需要的参数信息...
    var thePostData = {
      'room_name': that.data.m_liveName,
      'room_pass': that.data.m_livePass,
      'agent_id': that.data.m_curAgentID,
      'subject_id': that.data.m_curSubjectID,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/addLive';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('新建房间记录失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return;
        }
        // 再更新一些数据到新建记录当中...
        arrData.live.subject_id = that.data.m_curSubjectID;
        arrData.live.subject_name = that.data.m_curSubjectName;
        arrData.live.agent_id = that.data.m_curAgentID;
        arrData.live.agent_name = that.data.m_curAgentName;
        // 将新得到的房间记录存入父页面当中...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theNewLive = new Array();
        theNewLive.push(arrData.live);
        // 向前插入并更新房间记录到界面当中...
        let theArrLive = prevPage.data.m_arrLive;
        theArrLive = theNewLive.concat(theArrLive);
        prevPage.setData({ m_arrLive: theArrLive, m_total_num: theArrLive.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('新建房间记录失败！');
      }
    })
  },
  // 进行房间的保存操作...
  doLiveSave: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurLive = g_appData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'room_id': theCurLive.room_id,
      'room_name': that.data.m_liveName,
      'room_pass': that.data.m_livePass,
      'agent_id': that.data.m_curAgentID,
      'subject_id': that.data.m_curSubjectID,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/saveLive';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('更新房间记录失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurLive.indexID);
        let theArrLive = prevPage.data.m_arrLive;
        let thePrevLive = theArrLive[theIndex];
        thePrevLive.room_name = that.data.m_liveName;
        thePrevLive.room_pass = that.data.m_livePass;
        thePrevLive.agent_id = that.data.m_curAgentID;
        thePrevLive.subject_id = that.data.m_curSubjectID;
        thePrevLive.agent_name = that.data.m_curAgentName;
        thePrevLive.subject_name = that.data.m_curSubjectName;
        prevPage.setData({ m_arrLive: theArrLive });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('更新房间记录失败！');
      }
    })
  },
  // 点击删除按钮...
  doBtnDel: function (event) {
    Dialog.confirm({
      confirmButtonText: "删除",
      title: '房间：' + this.data.m_liveName,
      message: "确实要删除当前选中的房间记录吗？"
    }).then(() => {
      this.doLiveDel();
    }).catch(() => {
      console.log("Dialog - Cancel");
    });
  },
  // 进行房间的删除操作...
  doLiveDel: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurLive = g_appData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'room_id': theCurLive.room_id,
      'qrcode': theCurLive.qrcode,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/delLive';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('删除房间记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurLive.indexID);
        // 删除并更新房间记录到界面当中...
        prevPage.data.m_arrLive.splice(theIndex,1);
        let theArrLive = prevPage.data.m_arrLive;
        prevPage.setData({ m_arrLive: theArrLive, m_total_num: theArrLive.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('删除房间记录失败！');
      }
    })
  },
  // 房间名称发生变化...
  onNameChange: function (event) {
    this.data.m_liveName = event.detail;
  },
  // 房间密码发生变化...
  onPassChange: function (event) {
    this.data.m_livePass = event.detail;
  },
  // 科目发生选择变化...
  onSubjectChange: function (event) {
    const { value } = event.detail;
    let theNewSubjectID = parseInt(value);
    if (theNewSubjectID < 0 || theNewSubjectID >= this.data.m_arrSubject.length) {
      Notify('【所属科目】选择内容越界！');
      return;
    }
    // 获取到当前变化后的校长信息，并写入数据然后显示出来...
    let theCurSubject = this.data.m_arrSubject[theNewSubjectID];
    this.setData({ m_curSubjectID: theCurSubject.subject_id, m_curSubjectName: theCurSubject.subject_name });
  },
  // 所属机构发生选择变化...
  onAgentChange: function (event) {
    const { value } = event.detail;
    let theNewAgentID = parseInt(value);
    if (theNewAgentID < 0 || theNewAgentID >= this.data.m_arrAgent.length) {
      Notify('【所属机构】选择内容越界！');
      return;
    }
    // 获取到当前变化后的校长信息，并写入数据然后显示出来...
    let theCurAgent = this.data.m_arrAgent[theNewAgentID];
    this.setData({ m_curAgentID: theCurAgent.agent_id, m_curAgentName: theCurAgent.name });
  },
  /**
   * 生命周期函数--监听页面初次渲染完成
   */
  onReady: function () {

  },

  /**
   * 生命周期函数--监听页面显示
   */
  onShow: function () {

  },

  /**
   * 生命周期函数--监听页面隐藏
   */
  onHide: function () {

  },

  /**
   * 生命周期函数--监听页面卸载
   */
  onUnload: function () {

  },

  /**
   * 页面相关事件处理函数--监听用户下拉动作
   */
  onPullDownRefresh: function () {

  },

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {

  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {

  }
})