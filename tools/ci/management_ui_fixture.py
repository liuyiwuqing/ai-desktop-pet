#!/usr/bin/env python3
"""Loopback-only UI boundary fixture serving the actual compiled CORE console.

This is not a CORE integration test. State exists only in RAM; no database, broker or device.
"""
import argparse
import copy
import http.server
import json
import os
from datetime import datetime
from pathlib import Path
import socket
import secrets
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

class FixtureServer(http.server.ThreadingHTTPServer):
    """Loopback fixture server sized for a console full refresh.

    A full refresh opens many connections at once; the stdlib default listen
    backlog of 5 intermittently refuses one of them and the browser reports a
    bare TypeError: Failed to fetch. Handler threads are daemons so a stopped
    fixture cannot hang on a lingering keep-alive connection.
    """
    request_queue_size = 128
    daemon_threads = True


class Fixture(http.server.SimpleHTTPRequestHandler):
    lock=threading.Lock()
    state={'mode':'empty','config':copy.deepcopy(CONFIG),'revision':1,'requests':[],'submissions':[],'dropNext':False,
        'otaMode':'disabled','otaRequests':[],'otaSubmissions':[],'otaDrop':False,'otaBoot':'1'*32,'otaLastError':'','growthMode':'disabled',
        'messageSubmissions':[],'manualMessages':[],'messageNext':'success','messageDevices':False,'messageTemplates':False,'deviceName':'回归测试设备','renameSubmissions':[],'dropRenameNext':False,'renameReadDelay':0,'deviceReadDelay':0,'memoryMode':'success',
        'telemetryRequests':[],'disconnectRequests':[],'listMode':False,'listMessages':[],'listDeliveries':[],'listRequests':[],'listError':'','listDelay':0,
        'credentialMode':'active','credentialRevision':1,'credentialSecret':'','credentialSubmissions':[],
        'batchMode':'normal','batchRequests':[],'batchCancelled':False,'batchRetried':False,
        'multiBatchMode':'normal','multiBatchRequests':[],'multiBatchCancelled':False,'multiBatchRetried':False,
        'queueMode':'normal','queueRequests':[],'queueWaiting':3,'queueCancelled':False,
        'receiptMode':'normal','receiptRequests':[],
        'resendRequests':[],'resendNext':'success'}
    def log_message(self,*args):pass
    def json_response(self,status,value):
        payload=json.dumps(value,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)))
        self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(payload)
    def do_GET(self):
        if urlparse(self.path).path == "/api/v1/connectors/pull":
            return self.json_response(200, {"items": []})
        if urlparse(self.path).path == "/api/v1/templates":
            with self.lock:
                items=[{'templateId':'template-ui-fixture','definition':{'name':'评论提醒','title':'{title} · 模板','summary':'{content}','icon':'comment','sound':'chime','durationSeconds':8},'version':1,'deleted':False}] if self.state['messageTemplates'] else []
            return self.json_response(200, {"items": items, "variables": []})

        path=urlparse(self.path).path
        if path=='/__qa/state':
            if self.headers.get('X-QA-Key')!=CONTROL:return self.json_response(401,{'error':'unauthorized'})
            with self.lock:return self.json_response(200,self.state)
        if not path.startswith('/api/'):
            if path in ('/', '/index.html'):
                # Match the real console response policy so CSS-in-JS failures
                # cannot pass browser regressions only because CSP was absent.
                nonce=secrets.token_urlsafe(24)
                payload=(Path(self.directory)/'index.html').read_text().replace('__NEXUS_STYLE_NONCE__',nonce).encode()
                self.send_response(200)
                self.send_header('Content-Type','text/html; charset=utf-8')
                self.send_header('Content-Length',str(len(payload)))
                self.send_header('Cache-Control','no-store')
                self.send_header('Content-Security-Policy',"default-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'; style-src 'self' 'nonce-"+nonce+"'")
                self.end_headers();self.wfile.write(payload);return
            return super().do_GET()
        if self.headers.get('Authorization')!='Bearer '+TOKEN:return self.json_response(401,{'error':'unauthorized'})
        with self.lock:
            state=self.state;now=int(time.time());mode=state['mode']
            if path=='/api/v1/devices':
                delay=state['deviceReadDelay'];state['deviceReadDelay']=0
                if delay:time.sleep(delay)
                query=parse_qs(urlparse(self.path).query);stateKey=query.get('state',['all'])[0] or 'all'
                page=int(query.get('page',['1'])[0]);size=int(query.get('size',['100'])[0])
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                def device(device_id,name,enabled,online):
                    return {'deviceId':device_id,'name':name,'enabled':enabled,'online':online,'lastSeen':stamp,'wifiIp':'192.168.1.20','wifiRssi':-52,'wifiSsid':'HomeLab-2.4G','telemetryAt':stamp,
                        'wifiDisconnects24h':2,'deviceUnread':2,'waitingDeliveries':1 if state['queueCancelled'] else state['queueWaiting']}
                rows=[device('dodo-ui-fixture',state['deviceName'],True,True)]
                if state['messageDevices']:rows.append(device('dodo-ui-second','第二台设备',True,False));rows.append(device('dodo-ui-paused','已停用设备',False,False))
                if stateKey=='offline':rows=[]  # 唯一设备在线，离线筛选没有命中
                if stateKey=='disabled':rows=[]
                value={'items':rows[(page-1)*size:page*size],'total':3 if state['messageDevices'] else 1,'matched':len(rows),'page':page,'size':size,'state':stateKey,
                    'counts':{'online':1,'offline':1 if state['messageDevices'] else 0,'disabled':1 if state['messageDevices'] else 0}}
            elif state['listMode'] and path in ('/api/v1/messages','/api/v1/deliveries'):
                state['listRequests'].append(self.path)
                if state['listDelay']:time.sleep(state['listDelay'])
                if path.endswith('/'+state['listError']):return self.json_response(503,{'error':'unavailable'})
                query=parse_qs(urlparse(self.path).query);page=int(query.get('page',['1'])[0]);size=int(query.get('size',['30'])[0])
                messages=path.endswith('/messages');key='readStatus' if messages else 'status';status=query.get(key,[''])[0]
                rows=state['listMessages' if messages else 'listDeliveries']
                rows=[row for row in rows if not status or row[key]==status]
                if not messages:
                    reason=query.get('lastError',[''])[0]
                    if reason:rows=[row for row in rows if (row.get('lastError') or 'unknown')==reason]
                value={'items':copy.deepcopy(rows[(page-1)*size:page*size]),'total':len(rows),'page':page,'size':size}
            elif path.startswith('/api/v1/messages/') and path.endswith('/deliveries'):
                state['receiptRequests'].append(self.path)
                mode=state['receiptMode']
                if mode=='error':return self.json_response(503,{'error':'unavailable'})
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                items=[] if mode=='empty' else [
                    {'eventId':'receipt-read','deviceId':'dodo-ui-fixture','deviceName':'书房伙伴','status':'read','attempts':1,'lastError':None,
                     'nextAttemptAt':stamp,'receivedAt':stamp,'displayedAt':stamp,'readAt':stamp},
                    {'eventId':'receipt-pending','deviceId':'dodo-reading','deviceName':'阅读设备','status':'pending','attempts':0,'lastError':None,
                     'nextAttemptAt':stamp,'receivedAt':None,'displayedAt':None,'readAt':None}]
                if mode=='catchup' and len(state['receiptRequests'])>1:
                    # 第一次仍返回 pending，后续请求按设备已展示返回，供自动刷新断言使用。
                    items[1].update(status='displayed',attempts=1,receivedAt=stamp,displayedAt=stamp,nextAttemptAt=stamp)
                counts={}
                for item in items:counts[item['status']]=counts.get(item['status'],0)+1
                value={'messageId':path.split('/')[4],'total':len(items),'counts':counts,'items':items,'page':1,'size':100}
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
            elif path.endswith('/telemetry/disconnects'):
                state['disconnectRequests'].append(self.path)
                items=[] if mode=='empty' else [
                    {'observedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now-3300)),'reason':202,'disconnectedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now-3600))},
                    {'observedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now-7200)),'reason':201,'disconnectedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now-7500))},
                    {'observedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now-10800)),'reason':777,'disconnectedAt':None}]
                value={'deviceId':'dodo-ui-fixture','limit':int(parse_qs(urlparse(self.path).query).get('limit',['20'])[0]),
                    'retentionDays':30,'items':items}
            elif path.endswith('/telemetry'):
                query=parse_qs(urlparse(self.path).query);bucket=query.get('bucket',['5m'])[0]
                state['telemetryRequests'].append(self.path)
                step={'1m':60,'5m':300,'15m':900,'1h':3600,'1d':86400}.get(bucket,300)
                def moment(value,fallback):
                    # 控制台发的是 toISOString()，可能带小数秒；只认整秒会让整条历史接口失败。
                    try:return datetime.fromisoformat(value.replace('Z','+00:00')).timestamp()
                    except ValueError:return fallback
                start=moment(query.get('from',[''])[0],now-6*3600);end=moment(query.get('to',[''])[0],now)
                count=max(2,int((end-start)//step)+1) if mode!='empty' else 0
                points=[]
                for i in range(count):
                    heap=100000+(i*7919)%12000
                    points.append({'observedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(start+i*step)),
                        'receivedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(start+i*step+1)),'count':1,
                        'firmwareVersion':'0.4.0-ui-fixture','configRevision':state['revision'],'volume':10+(i*7)%40,
                        'brightness':state['config']['brightness'],'muted':False,'dnd':False,'unread':i%3,
                        'freeHeap':heap,'freeHeapMin':heap,'freeHeapMax':heap,'minFreeHeap':90000-(i%5)*100,
                        'audioUnderruns':(i*13)%4,'inferenceMaxMs':200+i,'displayMaxMs':20,'audioFaulted':i%17==5,
                        'wifiIp':'192.168.1.%d'%(20+i%3),'wifiRssi':-40-(i*7)%35,
                        'wifiSsid':'HomeLab-2.4G','wifiChannel':6+i%3,
                        'wifiBssid':'AA:BB:CC:DD:EE:%02X'%(i%8),'wifiChannelWidth':20 if i%3==0 else 40,
                        'wifiDisconnectReason':201 if i%5==0 else None,
                        'wifiDisconnectedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(start+i*step-1800)) if i%5==0 else None})
                value={'deviceId':'dodo-ui-fixture','from':query.get('from',[''])[0] or time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(start)),
                    'to':query.get('to',[''])[0] or time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(end)),'bucket':bucket,
                    'sampleSeconds':30,'retentionDays':30,'truncated':False,'points':points}
            elif path.endswith('/management'):
                telemetry=None if mode=='empty' else {'observedAt':now-(120 if mode=='stale' else 0),'firmwareVersion':'0.4.0-ui-fixture',
                    'configRevision':state['revision'],'configWritable':mode!='readonly','muted':False,'dnd':False,'volume':state['config']['volume'],
                    'brightness':state['config']['brightness'],'unread':0,'freeHeap':100000,'minFreeHeap':90000,
                    'wifiIp':'192.168.1.20','wifiRssi':-52,'wifiSsid':'HomeLab-2.4G','wifiChannel':6,
                    'wifiBssid':'AA:BB:CC:DD:EE:02','wifiChannelWidth':40,
                    'wifiDisconnectReason':201,'wifiDisconnectedAt':now-3600,'config':state['config']}
                value={'deviceId':'dodo-ui-fixture','fresh':mode not in ('empty','stale'),'canConfigure':mode not in ('empty','stale','readonly'),
                    'configBlock':'config_storage_readonly' if mode=='readonly' else 'config_readback_required' if mode in ('empty','stale') else '',
                    'telemetry':telemetry,'receivedAt':None,'configRequests':state['requests']}
            elif path.endswith('/audit'):value={'items':[{'sequence':1,'actor':'admin','event':'created','detail':'','createdAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}]}
            elif path.endswith('/credentials'):
                mode=state['credentialMode']
                if mode=='error':return self.json_response(503,{'error':'unavailable'})
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                value={'deviceId':'dodo-ui-fixture','username':'dev-dodo-ui-fixture',
                    'revision':0 if mode=='none' else state['credentialRevision'],'status':mode,
                    'acl':['topic readwrite nexus/v1/devices/dodo-ui-fixture/#'],
                    'issuedAt':None if mode=='none' else stamp,'rotatedAt':stamp if mode=='rotating' else None,
                    'previousExpiresAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(now+86400)) if mode=='rotating' else None,
                    'revokedAt':stamp if mode=='revoked' else None,'previousActive':mode=='rotating'}
            else:return self.json_response(404,{'error':'not_found'})
            self.json_response(200,value)
    def batch_response(self,message_id,action,query,state):
        """Per-message bulk cancel/retry: dry run explains every skip, apply needs the confirm token."""
        dry=query.get('dryRun',['false'])[0]=='true'
        confirm=query.get('confirm',[''])[0]
        state['batchRequests'].append({'action':action,'dryRun':dry,'confirm':confirm})
        if state['batchMode']=='fail':return self.json_response(503,{'error':'service_unavailable'})
        expected='CANCEL_MESSAGE_DELIVERIES' if action=='cancel' else 'RETRY_MESSAGE_DELIVERIES'
        if not dry and confirm!=expected:return self.json_response(400,{'error':'delivery_batch_confirmation_required'})
        empty=state['batchMode']=='empty'
        if action=='cancel':
            skipped=[] if empty else [
                {'eventId':'batch-attempted','deviceId':'dodo-attempted','status':'pending','attempts':1,'reason':'attempted'},
                {'eventId':'batch-failed','deviceId':'dodo-failed','status':'failed','attempts':1,'reason':'not_pending'}]
            candidates=[] if empty or state['batchCancelled'] else ['batch-ok-1','batch-ok-2']
        else:
            skipped=[] if empty else [
                {'eventId':'batch-pending','deviceId':'dodo-pending','status':'pending','attempts':0,'reason':'not_failed'},
                {'eventId':'batch-attempted','deviceId':'dodo-attempted','status':'pending','attempts':1,'reason':'not_failed'},
                {'eventId':'batch-failed','deviceId':'dodo-failed','status':'failed','attempts':1,'reason':'not_failed'}]
            candidates=[] if empty or state['batchRetried'] else ['batch-ok-3']
        applied=0
        if not dry:
            applied=len(candidates)
            if action=='cancel':state['batchCancelled']=True
            else:state['batchRetried']=True
            candidates=[]
        stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        return self.json_response(200,{'messageId':message_id,'action':action,'dryRun':dry,'applied':applied,
            'counts':{'deliveries':0 if empty else 4,'candidates':len(candidates),'skipped':len(skipped),'remaining':len(candidates)},
            'candidates':[{'eventId':event,'deviceId':'dodo-'+event,'status':'pending','attempts':0} for event in candidates],
            'skipped':skipped,'asOf':stamp})

    def multi_batch_response(self,action,query,value,state):
        """Cross-message cancel/retry: one dry run per action, then one token-guarded write."""
        dry=query.get('dryRun',['false'])[0]=='true'
        confirm=query.get('confirm',[''])[0]
        ids=value.get('messageIds') if isinstance(value.get('messageIds'),list) else []
        state['multiBatchRequests'].append({'action':action,'dryRun':dry,'confirm':confirm,'messageIds':ids})
        if state['multiBatchMode']=='reject':return self.json_response(404,{'error':'message_not_found'})
        expected='CANCEL_MESSAGE_DELIVERIES_BATCH' if action=='cancel' else 'RETRY_MESSAGE_DELIVERIES_BATCH'
        if not dry and confirm!=expected:return self.json_response(400,{'error':'delivery_batch_confirmation_required'})
        if not ids:return self.json_response(400,{'error':'invalid_request'})
        if len(ids)>20:return self.json_response(409,{'error':'batch_too_large'})
        done=state['multiBatchCancelled'] if action=='cancel' else state['multiBatchRetried']
        stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        messages=[];applied=0;candidates=0;skipped=0
        for message_id in ids:
            if action=='cancel':
                skip_rows=[('batch-attempted','attempted'),('batch-failed','not_pending')]
                plan_candidates=[] if done else ['batch-ok-1','batch-ok-2']
            else:
                skip_rows=[('batch-pending','not_failed'),('batch-attempted','not_failed'),('batch-failed','not_failed')]
                plan_candidates=[] if done else ['batch-ok-3']
            row={'messageId':message_id,'action':action,'dryRun':dry,'applied':0,
                'counts':{'deliveries':3,'candidates':len(plan_candidates),'skipped':len(skip_rows),'remaining':len(plan_candidates)},
                'candidates':[{'eventId':event,'deviceId':'dodo-'+event,'status':'pending','attempts':0} for event in plan_candidates],
                'skipped':[{'eventId':event,'deviceId':'dodo-'+event,'status':'pending','attempts':0,'reason':reason} for event,reason in skip_rows],
                'asOf':stamp}
            if not dry:
                row['applied']=len(plan_candidates);applied+=row['applied'];row['counts']['remaining']=0
            messages.append(row);candidates+=len(plan_candidates);skipped+=len(skip_rows)
        if not dry:
            if action=='cancel':state['multiBatchCancelled']=True
            else:state['multiBatchRetried']=True
        return self.json_response(200,{'action':action,'dryRun':dry,'applied':applied,
            'counts':{'messages':len(ids),'deliveries':3*len(ids),'candidates':0 if not dry else candidates,
                'skipped':skipped,'remaining':0 if not dry else candidates},
            'messages':messages,'asOf':stamp})

    def device_queue_response(self,device_id,query,state):
        """One device's pending+sent queue: dry run explains it, cancel needs the confirm token."""
        dry=query.get('dryRun',['false'])[0]=='true'
        confirm=query.get('confirm',[''])[0]
        state['queueRequests'].append({'deviceId':device_id,'dryRun':dry,'confirm':confirm})
        mode=state['queueMode']
        if mode=='error':return self.json_response(503,{'error':'service_unavailable'})
        if mode=='large':return self.json_response(409,{'error':'batch_too_large'})
        if not dry and confirm!='CANCEL_DEVICE_DELIVERIES':
            return self.json_response(400,{'error':'delivery_batch_confirmation_required'})
        empty=mode=='empty'
        cancelled=state['queueCancelled']
        before=0 if empty else 1 if cancelled else state['queueWaiting']
        skipped=[] if empty else [{'eventId':'queue-sent','deviceId':device_id,'status':'sent','attempts':1,'reason':'not_pending'}]
        candidates=[] if empty or cancelled else [
            {'eventId':'queue-ok-1','deviceId':device_id,'status':'pending','attempts':0},
            {'eventId':'queue-ok-2','deviceId':device_id,'status':'pending','attempts':0}]
        planned=len(candidates);applied=0
        if not dry:
            applied=planned;state['queueCancelled']=True
            candidates=[];state['queueWaiting']=1
        stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        return self.json_response(200,{'deviceId':device_id,'action':'cancel','dryRun':dry,'applied':applied,
            'counts':{'waiting':before,'candidates':planned,'skipped':len(skipped),'remaining':len(candidates)},
            'candidates':candidates,'skipped':skipped,'asOf':stamp})

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
                        deviceName='回归测试设备',messageDevices=False,messageTemplates=False,renameSubmissions=[],dropRenameNext=False,renameReadDelay=0,deviceReadDelay=0,memoryMode='success',telemetryRequests=[],disconnectRequests=[],growthMode='disabled',
                        listMode=False,listMessages=[],listDeliveries=[],listRequests=[],listError='',listDelay=0,
                        credentialMode='active',credentialRevision=1,credentialSecret='',credentialSubmissions=[],
                        batchMode='normal',batchRequests=[],batchCancelled=False,batchRetried=False,
                        multiBatchMode='normal',multiBatchRequests=[],multiBatchCancelled=False,multiBatchRetried=False,
                        queueMode='normal',queueRequests=[],queueWaiting=3,queueCancelled=False,
                        receiptMode='normal',receiptRequests=[],
                        resendRequests=[],resendNext='success')
                if value.get('messageReset'):state.update(messageSubmissions=[],manualMessages=[],messageNext='success',messageTemplates=False,resendRequests=[],resendNext='success')
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
                if 'resendNext' in value:state['resendNext']=value['resendNext']
                if value.get('otaReset'):state.update(otaMode='disabled',otaRequests=[],otaSubmissions=[],otaDrop=False,otaBoot='1'*32,otaLastError='')
                if 'otaMode' in value:state['otaMode']=value['otaMode']
                if 'growthMode' in value:state['growthMode']=value['growthMode']
                if 'otaDrop' in value:state['otaDrop']=value['otaDrop']
                if 'otaBoot' in value:state['otaBoot']=value['otaBoot']
                if 'otaLastError' in value:state['otaLastError']=value['otaLastError']
                if value.get('otaStage') and state['otaRequests']:state['otaRequests'][0]['status']='pending_reboot';state['otaMode']='pending_reboot'
                if 'mode' in value:state['mode']=value['mode']
                if 'memoryMode' in value:state['memoryMode']=value['memoryMode']
                if 'credentialMode' in value:
                    state['credentialMode']=value['credentialMode']
                    if value['credentialMode']=='none':state['credentialRevision']=0;state['credentialSecret']=''
                if 'batchMode' in value:
                    state['batchMode']=value['batchMode']
                    if value['batchMode']!='normal':state.update(batchCancelled=False,batchRetried=False)
                if 'multiBatchMode' in value:
                    state['multiBatchMode']=value['multiBatchMode']
                    if value['multiBatchMode']!='normal':state.update(multiBatchCancelled=False,multiBatchRetried=False)
                if 'queueMode' in value:state['queueMode']=value['queueMode']
                if 'queueWaiting' in value:state['queueWaiting']=int(value['queueWaiting'])
                if 'receiptMode' in value:state['receiptMode']=value['receiptMode']
                if 'deviceName' in value:state['deviceName']=value['deviceName']
                if 'messageDevices' in value:state['messageDevices']=bool(value['messageDevices'])
                if 'messageTemplates' in value:state['messageTemplates']=bool(value['messageTemplates'])
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
            if path in ('/api/v1/deliveries/batch-cancel','/api/v1/deliveries/batch-retry'):
                action='cancel' if path.endswith('cancel') else 'retry'
                return self.multi_batch_response(action,parse_qs(urlparse(self.path).query),value,state)
            if path.startswith('/api/v1/messages/') and '/deliveries/' in path:
                action=path.rsplit('/',1)[-1]
                if action not in ('cancel','retry'):return self.json_response(404,{'error':'not_found'})
                return self.batch_response(path.split('/')[4],action,parse_qs(urlparse(self.path).query),state)
            if path.startswith('/api/v1/messages/') and path.endswith('/resend'):
                source_id=path.split('/')[4]
                state['resendRequests'].append({'messageId':source_id,**value})
                outcome=state['resendNext'];state['resendNext']='success'
                if outcome=='reject':return self.json_response(409,{'error':'no_enabled_devices'})
                if outcome=='unauthorized':return self.json_response(401,{'error':'unauthorized'})
                existing=next((row for row in state['manualMessages'] if row.get('resendOf')==source_id and row.get('eventId')==value.get('eventId')),None)
                created=existing is None
                if outcome!='drop_before_save' and existing is None:
                    stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                    existing={'connectorId':'manual','source':'manual','eventId':value['eventId'],'resendOf':source_id,
                        'messageId':'resend-'+str(len(state['manualMessages'])+1),'eventType':'manual.test',
                        'title':'重发的消息','content':'重发内容','summary':'重发内容','sender':'','originalUrl':'',
                        'priority':'normal','receivedAt':stamp,'createdAt':stamp,'expiresAt':stamp,
                        'readAt':None,'readStatus':'unread','dataClass':'internal'}
                    state['manualMessages'].insert(0,existing)
                if outcome in ('save_then_drop','drop_before_save'):
                    self.close_connection=True;self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
                if outcome=='save_then_500':return self.json_response(503,{'error':'unavailable'})
                return self.json_response(201 if created else 200,{'messageId':existing['messageId'],'queued':2,'skipped':1})
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
            if path.startswith('/api/v1/devices/') and path.endswith('/deliveries/cancel'):
                return self.device_queue_response(path.split('/')[4],parse_qs(urlparse(self.path).query),state)
            if path.endswith('/credentials/rotate'):
                state['credentialSubmissions'].append(value)
                if state['credentialMode']=='revoked':return self.json_response(409,{'error':'device_credential_revoked'})
                if value.get('expectedRevision')!=state['credentialRevision']:return self.json_response(409,{'error':'device_credential_version_conflict'})
                state['credentialMode']='rotating';state['credentialRevision']+=1;state['credentialSecret']='B'*43
                stamp=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                return self.json_response(200,{'deviceId':'dodo-ui-fixture','username':'dev-dodo-ui-fixture','revision':state['credentialRevision'],
                    'status':'rotating','acl':['topic readwrite nexus/v1/devices/dodo-ui-fixture/#'],'secret':state['credentialSecret'],
                    'previousExpiresAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(time.time()+86400)),'rotatedAt':stamp})
            if path.endswith('/credentials/revoke'):
                state['credentialSubmissions'].append(value)
                if value.get('expectedRevision') not in (None,state['credentialRevision']):return self.json_response(409,{'error':'device_credential_version_conflict'})
                state['credentialMode']='revoked';state['credentialRevision']+=1;state['credentialSecret']=''
                self.send_response(204);self.end_headers();return
            if path.endswith('/credentials'):
                state['credentialSubmissions'].append(value)
                if state['credentialMode'] not in ('none','revoked'):return self.json_response(409,{'error':'device_credential_exists'})
                state['credentialRevision']=1 if state['credentialMode']=='none' else state['credentialRevision']+1
                state['credentialMode']='active';state['credentialSecret']='A'*43
                return self.json_response(201,{'deviceId':'dodo-ui-fixture','username':'dev-dodo-ui-fixture','revision':state['credentialRevision'],
                    'status':'active','acl':['topic readwrite nexus/v1/devices/dodo-ui-fixture/#'],'secret':state['credentialSecret'],
                    'issuedAt':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())})
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
    with FixtureServer(('127.0.0.1',args.port),handler) as server:
        print('UI boundary fixture ready',flush=True);server.serve_forever()
if __name__=='__main__':main()
