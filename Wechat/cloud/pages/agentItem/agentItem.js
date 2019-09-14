
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
    m_agentCPM: 0.50,
    m_agentName: '',
    m_agentAddr: '',
    m_agentPhone: '',
    m_curMasterName: '',
    m_curMasterID: 0,
    m_arrMaster: [],
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let isEdit = parseInt(options.edit);
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/getAgentFree';
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
          Notify('获取机构管理员列表失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        let arrMaster = JSON.parse(res.data);
        if (!(arrMaster instanceof Array)) {
          arrMaster = [];
        }
        // 保证输入的参数必须是数组类型...
        that.doShowAgent(isEdit, arrMaster);
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        Notify('获取机构管理员列表失败！');
      }
    });
  },
  // 显示具体的机构操作界面...
  doShowAgent: function (isEdit, arrMaster) {
    let strTitle = (isEdit ? '修改' : '添加') + ' - 机构';
    let theAgent = isEdit ? g_appData.m_curSelectItem : null;
    let theAgentCPM = isEdit ? theAgent.cpm : 0.50;
    let theAgentName = isEdit ? theAgent.name : '';
    let theAgentAddr = isEdit ? theAgent.addr : '';
    let theAgentPhone = isEdit ? theAgent.phone : '';
    let theCurMasterID = isEdit ? theAgent.master_id : 0;
    let theCurMasterName = isEdit ? theAgent.wx_nickname : '请选择';
    let theCurMasterIndex = 0;
    // 修改标题信息...
    wx.setNavigationBarTitle({ title: strTitle });
    // 如果是编辑状态，需要找到机构管理员所在的索引编号...
    if (isEdit && arrMaster.length > 0) {
      for (let index = 0; index < arrMaster.length; ++index) {
        if (arrMaster[index].user_id == theCurMasterID) {
          theCurMasterIndex = index;
          break;
        }
      }
    }
    // 应用到界面...
    this.setData({
      m_bEdit: isEdit,
      m_agentCPM: theAgentCPM,
      m_agentName: theAgentName,
      m_agentAddr: theAgentAddr,
      m_agentPhone: theAgentPhone,
      m_arrMaster: arrMaster,
      m_curMasterID: theCurMasterID,
      m_curMasterName: theCurMasterName,
      m_curMasterIndex: theCurMasterIndex,
    });
  },
  // 点击取消按钮...
  doBtnCancel: function (event) {
    wx.navigateBack();
  },
  // 点击保存按钮...
  doBtnSave: function (event) {
    if (this.data.m_agentName.length <= 0) {
      Notify('【机构名称】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_agentAddr.length <= 0) {
      Notify('【机构地址】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_agentPhone.length <= 0) {
      Notify('【机构电话】不能为空，请重新输入！');
      return;
    }
    if (parseFloat(this.data.m_agentCPM) <= 0) {
      Notify('【机构费率】不能为空，请重新输入！');
    }
    if (parseInt(this.data.m_curMasterID) <= 0) {
      Notify('【机构管理员】不能为空，请重新选择！');
      return;
    }
    // 根据不同的标志进行不同的接口调用...
    this.data.m_bEdit ? this.doAgentSave() : this.doAgentAdd();
  },
  // 进行机构的添加操作...
  doAgentAdd: function () {
    wx.showLoading({ title: '加载中' });
    let that = this;
    // 准备需要的参数信息...
    var thePostData = {
      'cpm': that.data.m_agentCPM,
      'name': that.data.m_agentName,
      'addr': that.data.m_agentAddr,
      'phone': that.data.m_agentPhone,
      'master_id': that.data.m_curMasterID,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/addAgent';
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
          Notify('新建机构记录失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return;
        }
        // 再更新一些数据到新建记录当中...
        arrData.agent.wx_nickname = that.data.m_curMasterName;
        // 将新得到的机构记录存入父页面当中...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theNewAgent = new Array();
        theNewAgent.push(arrData.agent);
        // 向前插入并更新机构记录到界面当中...
        let theArrAgent = prevPage.data.m_arrAgent;
        theArrAgent = theNewAgent.concat(theArrAgent);
        prevPage.setData({ m_arrAgent: theArrAgent, m_total_num: theArrAgent.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('新建机构记录失败！');
      }
    })
  },
  // 进行机构的保存操作...
  doAgentSave: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurAgent = g_appData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'agent_id': theCurAgent.agent_id,
      'cpm': that.data.m_agentCPM,
      'name': that.data.m_agentName,
      'addr': that.data.m_agentAddr,
      'phone': that.data.m_agentPhone,
      'master_id': that.data.m_curMasterID,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/saveAgent';
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
          Notify('更新机构记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurAgent.indexID);
        let theArrAgent = prevPage.data.m_arrAgent;
        let thePrevAgent = theArrAgent[theIndex];
        thePrevAgent.cpm = that.data.m_agentCPM;
        thePrevAgent.name = that.data.m_agentName;
        thePrevAgent.addr = that.data.m_agentAddr;
        thePrevAgent.phone = that.data.m_agentPhone;
        thePrevAgent.master_id = that.data.m_curMasterID;
        thePrevAgent.wx_nickname = that.data.m_curMasterName;
        prevPage.setData({ m_arrAgent: theArrAgent });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('更新机构记录失败！');
      }
    })
  },
  // 点击删除按钮...
  doBtnDel: function (event) {
    Dialog.confirm({
      confirmButtonText: "删除",
      title: '机构：' + this.data.m_agentName,
      message: "确实要删除当前选中的机构记录吗？"
    }).then(() => {
      this.doAgentDel();
    }).catch(() => {
      console.log("Dialog - Cancel");
    });
  },
  // 进行机构的删除操作...
  doAgentDel: function () {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurAgent = g_appData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'agent_id': theCurAgent.agent_id,
      'qrcode': theCurAgent.qrcode,
    }
    // 构造访问接口连接地址...
    let theUrl = g_appData.m_urlPrev + 'Mini/delAgent';
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
          Notify('删除机构记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurAgent.indexID);
        // 删除并更新机构记录到界面当中...
        prevPage.data.m_arrAgent.splice(theIndex,1);
        let theArrAgent = prevPage.data.m_arrAgent;
        prevPage.setData({ m_arrAgent: theArrAgent, m_total_num: theArrAgent.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('删除机构记录失败！');
      }
    })
  },
  // 机构名称发生变化...
  onNameChange: function (event) {
    this.data.m_agentName = event.detail;
  },
  // 机构地址发生变化...
  onAddrChange: function (event) {
    this.data.m_agentAddr = event.detail;
  },
  // 机构电话发生变化...
  onPhoneChange: function (event) {
    this.data.m_agentPhone = event.detail;
  },
  onCPMChange: function (event) {
    this.data.m_agentCPM = event.detail;
  },
  // 机构管理员发生选择变化...
  onMasterChange: function (event) {
    const { value } = event.detail;
    let theNewMasterID = parseInt(value);
    if (theNewMasterID < 0 || theNewMasterID >= this.data.m_arrMaster.length) {
      Notify('【机构管理员】选择内容越界！');
      return;
    }
    // 获取到当前变化后的机构管理员信息，并写入数据然后显示出来...
    let theCurMaster = this.data.m_arrMaster[theNewMasterID];
    this.setData({ m_curMasterID: theCurMaster.user_id, m_curMasterName: theCurMaster.wx_nickname });
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