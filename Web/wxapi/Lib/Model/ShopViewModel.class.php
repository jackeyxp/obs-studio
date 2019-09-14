<?php

class ShopViewModel extends ViewModel {
    public $viewFields = array(
        'Shop'=>array('*','_type'=>'LEFT'), //Must be LEFT 返回左表所有数据，即使右表没有匹配的
        'User'=>array('user_id','wx_nickname','_on'=>'Shop.master_id=User.user_id','_type'=>'LEFT'),
        'Agent'=>array('agent_id','name'=>'agent_name','_on'=>'Shop.agent_id=Agent.agent_id')
    );
}
?>