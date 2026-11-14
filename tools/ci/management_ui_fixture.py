#!/usr/bin/env python3
"""Loopback-only UI boundary fixture serving the actual compiled CORE console.

This is not a CORE integration test. State exists only in RAM; no database, broker or device.
"""
import argparse
import copy
import http.server
import json
import os
from pathlib import Path
import socket
import threading
import time
from urllib.parse import parse_qs, urlparse

TOKEN='ui-regression-fixture-token-at-least-32-characters'
CONTROL='isolated-ui-fixture-control'
OTA_TARGET={'releaseId':'release-ui','version':'0.4.1','imageSha256':'c'*64,'imageSize':1024,'releaseSequence':10}
OTA_IDENTITY={'project':'nexus_pet','board':'dodo-s3-lcd','modelSha256':'none','partitionSha256':'a'*64,'configSchema':1,'growthSchema':4}
CONFIG={'schemaVersion':1,'timezone':'Asia/Shanghai','language':'zh-CN','volume':25,'brightness':80,'muted':False,
    'dnd':{'enabled':False,'manual':False,'startMinute':1320,'endMinute':480,'allowCritical':True},
    'message':{'queueDepth':8,'durationMs':8000}}

class Fixture(http.server.SimpleHTTPRequestHandler):
    lock=threading.Lock()
    state={'mode':'empty','config':copy.deepcopy(CONFIG),'revision':1,'requests':[],'submissions':[],'dropNext':False,
        'otaMode':'disabled','otaRequests':[],'otaSubmissions':[],'otaDrop':False,'otaBoot':'1'*32,'otaLastError':'','growthMode':'disabled',
        'messageSubmissions':[],'manualMessages':[],'messageNext':'success','deviceName':'回归测试设备','renameSubmissions':[],'dropRenameNext':False,'renameReadDelay':0,'deviceReadDelay':0,'memoryMode':'success',
        'listMode':False,'listMessages':[],'listDeliveries':[],'listRequests':[],'listError':'','listDelay':0}
    def log_message(self,*args):pass
    def json_response(self,status,value):
        payload=json.dumps(value,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)))
        self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(payload)
    def do_GET(self):
        path=urlparse(self.path).path
        if path=='/__qa/state':
            if self.headers.get('X-QA-Key')!=CONTROL:return self.json_response(401,{'error':'unauthorized'})
            with self.lock:return self.json_response(200,self.state)
        if not path.startswith('/api/'):
            if path=='/':self.path='/index.html'
            return super().do_GET()
        if self.headers.get('Authorization')!='Bearer '+TOKEN:return self.json_response(401,{'error':'unauthorized'})
        with self.lock:
            state=self.state;now=int(time.time());mode=state['mode']
            if path=='/api/v1/devices':
                delay=state['deviceReadDelay'];state['deviceReadDelay']=0
                if delay:time.sleep(delay)
                value={'items':[{'deviceId':'dodo-ui-fixture','name':state['deviceName'],'enabled':True,'online':True,'lastSeen':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}]}
            elif state['listMode'] and path in ('/api/v1/messages','/api/v1/deliveries'):
                state['listRequests'].append(self.path)
                if state['listDelay']:time.sleep(state['listDelay'])
                if path.endswith('/'+state['listError']):return self.json_response(503,{'error':'unavailable'})
                query=parse_qs(urlparse(self.path).query);page=int(query.get('page',['1'])[0]);size=int(query.get('size',['30'])[0])
                messages=path.endswith('/messages');key='readStatus' if messages else 'status';status=query.get(key,[''])[0]
                rows=state['listMessages' if messages else 'listDeliveries']
                rows=[row for row in rows if not status or row[key]==status]
                value={'items':copy.deepcopy(rows[(page-1)*size:page*size]),'total':len(rows),'page':page,'size':size}
            elif path in ('/api/v1/connectors','/api/v1/deliveries'):value={'items':[],'total':0,'page':1,'size':30}
            elif path=='/api/v1/memory':value={'configured':state['memoryMode']!='disabled','storedCopies':1,'pendingDeletions':2,'deletedCopies':3,'deletionErrors':1,'invalidationPolicy':'stop_retrieval_and_delete'}
            elif path=='/api/v1/status':value={'database':'ready','mqtt':'ready','mqttError':'','schemaVersion':9}
            elif path=='/api/v1/pet':value={'level':1,'levelProgress':0,'experience':0,'coins':0,'bond':0,'completedFocus':0,'activeSession':None}
            elif path=='/api/v1/pet/focus':value={'items':[]}
            elif path=='/api/v1/pet/devices':
                growth_mode=state['growthMode']
                if growth_mode=='error':return self.json_response(503,{'error':'unavailable'})
                if growth_mode=='delayed':time.sleep(1)
                growth={'level':2,'experience':15,'experienceToNext':128,'coins':72,'intimacy':10,'mood':20,'energy':80,'hunger':60,'health':100,
                    'state':'idle','timeReady':growth_mode!='time_unavailable','unsaved':growth_mode=='unsaved','storageReady':growth_mode!='readonly','saveRecovered':growth_mode=='recovered'}
                snapshot=None if growth_mode=='waiting' else {'observedAt':now-(120 if growth_mode=='stale' else 0),'growth':growth}
                value={'enabled':growth_mode!='disabled','items':[] if growth_mode=='empty' else [{'deviceId':'dodo-ui-fixture','name':state['deviceName'],
                    'deviceEnabled':growth_mode!='device_disabled','fresh':growth_mode not in ('stale','waiting'),'snapshot':snapshot}]}
            elif path=='/api/v1/messages':
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                value={'items':[{'messageId':'purged-ui-fixture','connectorId':'fixture','source':'halo','eventType':'comment.created',
                    'title':'','content':'','summary':'','sender':'','originalUrl':'','priority':'normal','receivedAt':stamp,'createdAt':stamp,
                    'expiresAt':stamp,'readAt':None,'readStatus':'unread','dataClass':'public_comment','contentPurgedAt':stamp}],
                    'total':1,'page':1,'size':30}
                value['items']=copy.deepcopy(state['manualMessages'])+value['items'];value['total']=len(value['items'])
            elif path=='/api/v1/ota/releases':value={'enabled':state['otaMode']!='disabled','items':[] if state['otaMode']=='no_releases' else [{'releaseId':'release-ui','target':OTA_TARGET,'identity':OTA_IDENTITY,'createdAt':'2026-11-13T00:00:00Z'}]}
            elif path.endswith('/ota'):
                mode=state['otaMode'];requests=state['otaRequests'];request=requests[0] if requests else None
                observation=None if mode=='empty' else {'bootId':state['otaBoot'],'state':'pending_reboot' if mode=='pending_reboot' else 'idle','observedAt':now-(120 if mode=='stale' else 0),
                    'available':mode!='unavailable','requestId':request['requestId'] if request else '', 'rebootId':'','writtenBytes':1024 if request else 0,'reservedSequence':0,'identity':OTA_IDENTITY}
                value={'enabled':mode!='disabled','deviceId':'dodo-ui-fixture','fresh':mode not in ('empty','stale'), 'canInstall':mode in ('fresh','no_releases') and not requests,
                    'installBlock':'ota_device_unavailable' if mode=='unavailable' else 'ota_readback_required' if mode in ('empty','stale') else 'ota_deployment_in_progress' if requests else '',
                    'canReboot':mode=='pending_reboot','observation':observation,'deployments':requests}
            elif path.endswith('/management'):
                telemetry=None if mode=='empty' else {'observedAt':now-(120 if mode=='stale' else 0),'firmwareVersion':'0.4.0-ui-fixture',
                    'configRevision':state['revision'],'configWritable':mode!='readonly','muted':False,'dnd':False,'volume':state['config']['volume'],
                    'brightness':state['config']['brightness'],'unread':0,'freeHeap':100000,'minFreeHeap':90000,'config':state['config']}
                value={'deviceId':'dodo-ui-fixture','fresh':mode not in ('empty','stale'),'canConfigure':mode not in ('empty','stale','readonly'),
                    'configBlock':'config_storage_readonly' if mode=='readonly' else 'config_readback_required' if mode in ('empty','stale') else '',
                    'telemetry':telemetry,'receivedAt':None,'configRequests':state['requests']}
            elif path.endswith('/audit'):value={'items':[{'sequence':1,'actor':'admin','event':'created','detail':'','createdAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}]}
            else:return self.json_response(404,{'error':'not_found'})
            self.json_response(200,value)
    def do_POST(self):
        if urlparse(self.path).path == "/api/v1/session":
            if self.headers.get("Authorization") != "Bearer " + TOKEN:
                return self.json_response(401, {"error": "unauthorized"})
            return self.json_response(200, {"role": "admin", "actor": "fixture-admin"})

        path=urlparse(self.path).path
        raw=self.rfile.read(int(self.headers.get('Content-Length','0')))
        try:value=json.loads(raw) if raw else {}
        except ValueError:return self.json_response(400,{'error':'invalid_request'})
        with self.lock:
            state=self.state
            if path=='/__qa/state':
                if self.headers.get('X-QA-Key')!=CONTROL:return self.json_response(401,{'error':'unauthorized'})
                if value.get('reset'):
                    state.update(mode='empty',config=copy.deepcopy(CONFIG),revision=1,requests=[],submissions=[],dropNext=False,
                        deviceName='回归测试设备',renameSubmissions=[],dropRenameNext=False,renameReadDelay=0,deviceReadDelay=0,memoryMode='success',growthMode='disabled',
                        listMode=False,listMessages=[],listDeliveries=[],listRequests=[],listError='',listDelay=0)
                if value.get('messageReset'):state.update(messageSubmissions=[],manualMessages=[],messageNext='success')
                if value.get('listReset'):
                    stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                    state.update(listMode=True,listError='',listDelay=0,listRequests=[],listMessages=[{
                        'messageId':'list-'+str(i),'connectorId':'fixture','source':'manual','eventType':'manual.test',
                        'title':('未读消息 ' if i<31 else '已读消息 ')+str(i),'content':'列表筛选回归正文','summary':'列表筛选回归',
                        'sender':'','originalUrl':'','priority':'normal','receivedAt':stamp,'createdAt':stamp,'expiresAt':stamp,
                        'readAt':None if i<31 else stamp,'readStatus':'unread' if i<31 else 'read','dataClass':'internal'} for i in range(33)],
                        listDeliveries=[{'eventId':'delivery-'+str(i),'messageId':'list-'+str(i),'deviceId':'dodo-ui-fixture',
                            'title':'投递消息 '+str(i),'status':status,'attempts':1,'lastError':'ack_timeout' if status=='failed' else None,
                            'nextAttemptAt':stamp,'readAt':None} for i,status in enumerate(['failed']*31+['pending','sent','received','displayed','read','expired'])])
                if 'listError' in value:state['listError']=value['listError']
                if 'listDelay' in value:state['listDelay']=min(max(float(value['listDelay']),0),2)
                if value.get('listEmpty'):state['listMessages']=[];state['listDeliveries']=[]
                if 'messageNext' in value:state['messageNext']=value['messageNext']
                if value.get('otaReset'):state.update(otaMode='disabled',otaRequests=[],otaSubmissions=[],otaDrop=False,otaBoot='1'*32,otaLastError='')
                if 'otaMode' in value:state['otaMode']=value['otaMode']
                if 'growthMode' in value:state['growthMode']=value['growthMode']
                if 'otaDrop' in value:state['otaDrop']=value['otaDrop']
                if 'otaBoot' in value:state['otaBoot']=value['otaBoot']
                if 'otaLastError' in value:state['otaLastError']=value['otaLastError']
                if value.get('otaStage') and state['otaRequests']:state['otaRequests'][0]['status']='pending_reboot';state['otaMode']='pending_reboot'
                if 'mode' in value:state['mode']=value['mode']
                if 'memoryMode' in value:state['memoryMode']=value['memoryMode']
                if 'deviceName' in value:state['deviceName']=value['deviceName']
                if value.get('dropRenameNext'):state['dropRenameNext']=True
                if 'renameReadDelay' in value:state['renameReadDelay']=min(max(float(value['renameReadDelay']),0),2)
                if value.get('advance'):state['revision']+=1;state['config']['volume']=35
                if value.get('dropNext'):state['dropNext']=True
                if value.get('apply') and state['requests']:
                    request=state['requests'][0];request.update(status='applied',confirmationSource='ack',reportedRevision=state['revision']+1)
                    state['revision']+=1;state['config']=copy.deepcopy(request['requestedConfig'])
                return self.json_response(200,{'ok':True})
            if self.headers.get('Authorization')!='Bearer '+TOKEN:return self.json_response(401,{'error':'unauthorized'})
            if state['listMode'] and path.startswith('/api/v1/messages/') and path.endswith('/read'):
                for row in state['listMessages']:
                    if row['messageId']==path.split('/')[-2]:row['readStatus']='read';row['readAt']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                self.send_response(204);self.end_headers();return
            if state['listMode'] and path.startswith('/api/v1/deliveries/') and path.endswith('/retry'):
                for row in state['listDeliveries']:
                    if row['eventId']==path.split('/')[-2]:row['status']='pending';row['lastError']=None
                self.send_response(204);self.end_headers();return
            if path=='/api/v1/messages':
                state['messageSubmissions'].append(value)
                outcome=state['messageNext'];state['messageNext']='success'
                if outcome=='reject':return self.json_response(400,{'error':'invalid_request'})
                if outcome=='unauthorized':return self.json_response(401,{'error':'unauthorized'})
                existing=next((row for row in state['manualMessages'] if row['eventId']==value.get('eventId') and row['connectorId']==value.get('connectorId')),None)
                created=existing is None
                if outcome!='drop_before_save' and existing is None:
                    stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                    existing={**value,'messageId':'manual-'+str(len(state['manualMessages'])+1),'receivedAt':stamp,'createdAt':stamp,
                        'summary':value['content'],'sender':'','originalUrl':'','expiresAt':stamp,'readAt':None,'readStatus':'unread','dataClass':'internal'}
                    state['manualMessages'].insert(0,existing)
                if outcome in ('save_then_drop','drop_before_save'):
                    self.close_connection=True;self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
                if outcome=='save_then_500':return self.json_response(503,{'error':'unavailable'})
                if outcome=='invalid_ack':return self.json_response(201,{'invalid':'acknowledgment'})
                return self.json_response(201 if created else 200,{'messageId':existing['messageId']})
            if path=='/api/v1/ota/releases':
                state['otaSubmissions'].append(value)
                return self.json_response(201,{'created':True})
            if '/ota/deployments' in path:
                state['otaSubmissions'].append(value)
                if state['otaLastError']:return self.json_response(409,{'error':state['otaLastError']})
                if state['otaDrop']:
                    state['otaDrop']=False;self.close_connection=True;self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                if path.endswith('/reboot'):
                    state['otaRequests'][0]['commands'].append({'commandId':value['requestId'],'operation':'reboot','status':'pending','attempts':0,'expiresAt':stamp})
                    state['otaRequests'][0]['status']='rebooting';state['otaMode']='busy'
                elif not any(row['requestId']==value['requestId'] for row in state['otaRequests']):
                    state['otaRequests'].insert(0,{'requestId':value['requestId'],'releaseId':value['releaseId'],'target':OTA_TARGET,'status':'pending','lastError':'','createdAt':stamp,'commands':[]})
                return self.json_response(202,{'created':True,'requestId':value['requestId']})
            if path=='/api/v1/memory/search':
                mode=state['memoryMode']
                if mode=='disabled':return self.json_response(409,{'error':'memory_not_configured'})
                if mode=='error':return self.json_response(502,{'error':'memory_search_failed'})
                if mode=='delayed':time.sleep(1)
                return self.json_response(200,{'items':[] if mode=='empty' else [{'messageId':'fixture-comment','content':'希望桌面伙伴能在专注结束后提醒我休息，也能记住常用的工作安排。' if mode=='visual' else '公开评论 <img src=x onerror=alert(1)>','summary':'读者希望增加专注结束提醒，并保存常用工作安排。' if mode=='visual' else 'AI 推断摘要',
                    'originalUrl':'https://example.invalid/post#comment' if mode=='visual' else 'javascript:alert(1)','verifiedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}],
                    'partial':mode=='partial','dataPolicy':'verified_public_comments_only'})
            if path.endswith('/config-requests'):
                state['submissions'].append(value)
                if state['dropNext']:
                    state['dropNext']=False;self.close_connection=True
                    self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
                for old in state['requests']:
                    if old['requestId']==value.get('requestId'):return self.json_response(200,old)
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                request={'requestId':value['requestId'],'expectedRevision':value['expectedRevision'],'requestedConfig':value['config'],
                    'status':'pending','attempts':0,'reportedRevision':None,'deviceCode':None,'lastError':'','confirmationSource':None,
                    'createdAt':stamp,'updatedAt':stamp,'expiresAt':stamp}
                state['requests'].insert(0,request);return self.json_response(202,request)
            return self.json_response(404,{'error':'not_found'})
    def do_PATCH(self):
        if self.headers.get('Authorization')!='Bearer '+TOKEN:return self.json_response(401,{'error':'unauthorized'})
        if urlparse(self.path).path!='/api/v1/devices/dodo-ui-fixture/name':return self.json_response(404,{'error':'not_found'})
        try:value=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))))
        except ValueError:return self.json_response(400,{'error':'invalid_request'})
        with self.lock:
            state=self.state;state['renameSubmissions'].append(value)
            desired=value['name'].strip()
            if state['deviceName']!=desired and state['deviceName']!=value['expectedName']:
                return self.json_response(409,{'error':'device_name_conflict'})
            state['deviceName']=desired
            state['deviceReadDelay']=state['renameReadDelay'];state['renameReadDelay']=0
            if state['dropRenameNext']:
                state['dropRenameNext']=False;self.close_connection=True
                self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
            return self.json_response(200,{'deviceId':'dodo-ui-fixture','name':desired})

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--frontend',type=Path,required=True);parser.add_argument('--port',type=int,default=18799)
    args=parser.parse_args();root=args.frontend.resolve()
    Fixture.state['otaManifestPath']=os.environ.get('PET_OTA_UI_MANIFEST','')
    if not (root/'index.html').is_file():parser.error('build the actual CORE frontend first')
    handler=lambda *a,**kw:Fixture(*a,directory=str(root),**kw)
    with http.server.ThreadingHTTPServer(('127.0.0.1',args.port),handler) as server:
        print('UI boundary fixture ready',flush=True);server.serve_forever()
if __name__=='__main__':main()
