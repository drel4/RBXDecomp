local function near(a,b) assert(math.abs(a-b)<0.0001,tostring(a)..' != '..tostring(b)) end
local function vector(a,b) near((a-b).magnitude,0) end
local a=Vector3.new(3,4,0)
near(a.magnitude,5)
vector(a.unit,Vector3.new(.6,.8,0))
vector(2*a,Vector3.new(6,8,0))
vector(a*Vector3.new(2,3,4),Vector3.new(6,12,0))
vector(12/Vector3.new(2,3,4),Vector3.new(6,4,3))
vector(-a,Vector3.new(-3,-4,0))
near(a:Dot(a),25)
vector(Vector3.new(1,0,0):Cross(Vector3.new(0,1,0)),Vector3.new(0,0,1))
vector(a:lerp(Vector3.new(5,6,0),.5),Vector3.new(4,5,0))
local f=CFrame.new(3,5,7)*CFrame.Angles(0,math.pi/2,0)
vector(f*Vector3.new(0,0,-2),Vector3.new(1,5,7))
vector(f.lookVector,Vector3.new(-1,0,0))
vector(f:inverse()*(f*a),a)
vector(f:pointToObjectSpace(f:pointToWorldSpace(a)),a)
local p,q=f:pointToWorldSpace(a,Vector3.new())
vector(p,f*a);vector(q,f.p)
assert(CFrame.new(unpack({f:components()}))==f)
local x,y,z=CFrame.Angles(.2,.3,.4):toEulerAnglesXYZ()
near(x,.2);near(y,.3);near(z,.4)
vector(CFrame.new(Vector3.new(),Vector3.new(0,0,-5)).lookVector,Vector3.new(0,0,-1))
vector(CFrame.new(Vector3.new(),Vector3.new(0,-5,0)).lookVector,Vector3.new(0,-1,0))
vector(CFrame.fromAxisAngle(Vector3.new(0,1,0),math.pi/2).lookVector,f.lookVector)
vector(CFrame.new(0,0,0,0,math.sin(math.pi/4),0,math.cos(math.pi/4)).lookVector,f.lookVector)
assert(not pcall(function() a.x=3 end))
assert(not pcall(function() return Instance.new('NotAnEngineClass') end))
local part=Instance.new('Part',workspace)
part.Name='BindingsPart'
assert(part:IsA('PVInstance') and part:IsA('Instance') and not part:IsA('Model'))
assert(part.Anchored==false and part.CanCollide==true)
vector(part.Size,Vector3.new(4,1.2,2))
part.Size=Vector3.new(8,2,6)
vector(part.size,Vector3.new(8,2,6))
part.CFrame=f
vector(part.Position,Vector3.new(3,5,7))
part.Position=Vector3.new(9,10,11)
vector(part.CFrame.p,Vector3.new(9,10,11))
vector(part.CFrame.lookVector,f.lookVector)
local msg=Instance.new('Message');msg.Text='engine default materialized'
assert(msg.Text=='engine default materialized')
local joint=Instance.new('Weld',part);joint.Part0=part;joint.C0=f
assert(joint:IsA('JointInstance') and joint.Part0==part and joint.C0==f)
local value=Instance.new('IntValue',workspace)
assert(value.Value==0)
value.Value=3.7
assert(value.Value==3)
local changes,finished=0,false
local connection=value.Changed:connect(function(v)
    assert(v==7)
    changes=changes+1
    wait(.05)
    finished=true
end)
assert(connection.connected and connection.Connected)
value.Value=7;value.Value=7
wait(.2)
assert(changes==1 and finished)
connection:disconnect()
assert(not connection.connected)
delay(.05,function() value.Value=9 end)
local result=value.Changed:wait()
assert(result==9)
delay(.05,function()
    local child=Instance.new('Part')
    child.Name='SignalChild'
    child.Parent=workspace
end)
local child=workspace.ChildAdded:Wait()
assert(child.Name=='SignalChild')
local added=0
local hierarchy=workspace.DescendentAdded:Connect(function(instance) added=added+1 end)
local tree=Instance.new('Model')
Instance.new('Part',tree)
tree.Parent=workspace
wait(.1)
assert(added==2)
hierarchy:Disconnect()
local pass=Instance.new('BoolValue',workspace)
pass.Name='BindingsPassed';pass.Value=true
print('value types, defaults and yielding signals passed')
