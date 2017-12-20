//  TNSDebugging.h
//  NativeScript
//
//  Created by Yavor Georgiev on 30.04.15.
//  Copyright (c) 2015 г. Telerik. All rights reserved.
//

#import <JavaScriptCore/JSStringRefCF.h>
#import <JavaScriptCore/JavaScript.h>
#import <NativeScript.h>
#import <UIKit/UIApplication.h>

#include <errno.h>
#include <netinet/in.h>
#include <notify.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

static TNSRuntimeInspector* inspector = nil;
static BOOL isWaitingForDebugger = NO;

typedef void (^TNSInspectorProtocolHandler)(NSString* message, NSError* error);

typedef void (^TNSInspectorSendMessageBlock)(NSString* message);

typedef TNSInspectorProtocolHandler (^TNSInspectorFrontendConnectedHandler)(
    TNSInspectorSendMessageBlock sendMessageToFrontend, NSError* error);

typedef void (^TNSInspectorIoErrorHandler)(NSObject* dummy /*make compatible with CheckError macro*/, NSError* error);

#define CheckError(retval, handler)                                  \
    ({                                                               \
        int errorCode = (int)retval;                                 \
        BOOL success = NO;                                           \
        if (errorCode == 0)                                          \
            success = YES;                                           \
        else if (errorCode == -1)                                    \
            errorCode = errno;                                       \
        if (!success)                                                \
            handler(nil, [NSError errorWithDomain:NSPOSIXErrorDomain \
                                             code:errorCode          \
                                         userInfo:nil]);             \
        success;                                                     \
    })

#define NOTIFICATION(name)                                                      \
    [[NSString stringWithFormat:@"%@:NativeScript.Debug.%s",                    \
                                [[NSBundle mainBundle] bundleIdentifier], name] \
        UTF8String]

static dispatch_source_t TNSCreateInspectorServer(
    TNSInspectorFrontendConnectedHandler connectedHandler,
    TNSInspectorIoErrorHandler ioErrorHandler) {
    dispatch_queue_t queue = dispatch_get_global_queue(0, 0);

    dispatch_fd_t listenSocket = socket(PF_INET, SOCK_STREAM, 0);
    int so_reuseaddr = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr,
               sizeof(so_reuseaddr));
    struct sockaddr_in addr = {
        sizeof(addr), AF_INET, htons(18181), { INADDR_ANY }, { 0 }
    };
    if (!CheckError(
            bind(listenSocket, (const struct sockaddr*)&addr, sizeof(addr)),
            connectedHandler)) {
        return nil;
    }

    if (!CheckError(listen(listenSocket, 0), connectedHandler)) {
        return nil;
    }

    __block dispatch_source_t listenSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, listenSocket, 0, queue);

    dispatch_source_set_event_handler(listenSource, ^{
      if (inspector && [inspector hasFrontends]) {
          // Only one connection is supported at a time. Accept and immediately close new attempt's socket
          dispatch_fd_t secondInspectorSocket = accept(listenSocket, NULL, NULL);
          close(secondInspectorSocket);
          return;
      }

      __block dispatch_fd_t newSocket = accept(listenSocket, NULL, NULL);

      __block dispatch_io_t io = 0;
      __block TNSInspectorProtocolHandler protocolHandler = nil;
      __block TNSInspectorIoErrorHandler dataSocketErrorHandler = ^(NSObject* dummy, NSError* error) {
        assert(io);
        if (io) {
            dispatch_io_close(io, DISPATCH_IO_STOP);
            io = 0;
        }
        assert(newSocket);
        if (newSocket) {
            close(newSocket);
            newSocket = 0;
        }

        if (protocolHandler) {
            protocolHandler(nil, error);
        }

        if (ioErrorHandler) {
            ioErrorHandler(nil, error);
        }
      };

      io = dispatch_io_create(DISPATCH_IO_STREAM, newSocket, queue, ^(int error) {
        CheckError(error, dataSocketErrorHandler);
      });

      TNSInspectorSendMessageBlock sender = ^(NSString* message) {
        if (!io) {
            return;
        }

        NSUInteger length = [message
            lengthOfBytesUsingEncoding:NSUTF16LittleEndianStringEncoding];

        uint8_t* buffer = (uint8_t*)malloc(length + sizeof(uint32_t));

        *(uint32_t*)buffer = htonl(length);

        [message getBytes:&buffer[sizeof(uint32_t)]
                 maxLength:length
                usedLength:NULL
                  encoding:NSUTF16LittleEndianStringEncoding
                   options:0
                     range:NSMakeRange(0, message.length)
            remainingRange:NULL];

        dispatch_data_t data = dispatch_data_create(buffer, length + sizeof(uint32_t), queue, ^{
          free(buffer);
        });

        dispatch_io_write(io, 0, data, queue,
                          ^(bool done, dispatch_data_t data, int error) {
                            CheckError(error, dataSocketErrorHandler);
                          });
      };

      protocolHandler = connectedHandler(sender, nil);
      if (!protocolHandler) {
          dataSocketErrorHandler(nil, nil);
          return;
      }

      __block dispatch_io_handler_t ioHandler = ^(bool done, dispatch_data_t data,
                                                  int error) {
        if (!CheckError(error, dataSocketErrorHandler)) {
            return;
        }

        const void* bytes = [(NSData*)data bytes];
        if (!bytes) {
            dataSocketErrorHandler(nil, nil);
            return;
        }

        uint32_t length = ntohl(*(uint32_t*)bytes);
        dispatch_io_set_low_water(io, length);
        dispatch_io_read(io, 0, length, queue,
                         ^(bool done, dispatch_data_t data, int error) {
                           if (!CheckError(error, dataSocketErrorHandler)) {
                               return;
                           }

                           NSString* payload = [[NSString alloc]
                               initWithData:(NSData*)data
                                   encoding:NSUTF16LittleEndianStringEncoding];
                           protocolHandler(payload, nil);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-retain-cycles"
                           dispatch_io_read(io, 0, 4, queue, ioHandler);
#pragma clang diagnostic pop
                         });
      };

      dispatch_io_read(io, 0, 4, queue, ioHandler);
    });
    dispatch_source_set_cancel_handler(listenSource, ^{
      isWaitingForDebugger = NO;
      listenSource = nil;
      close(listenSocket);
    });
    dispatch_resume(listenSource);

    return listenSource;
}

static void TNSInspectorUncaughtExceptionHandler(NSException* exception) {
    JSStringRef exceptionMessage = JSStringCreateWithUTF8CString(exception.description.UTF8String);

    JSValueRef errorArguments[] = {
        JSValueMakeString(inspector.runtime.globalContext, exceptionMessage)
    };
    JSObjectRef error = JSObjectMakeError(inspector.runtime.globalContext, 1,
                                          errorArguments, NULL);

    [inspector reportFatalError:error];
}

static void TNSEnableRemoteInspector(int argc, char** argv,
                                     TNSRuntime* runtime) {
    __block dispatch_source_t listenSource = nil;

    dispatch_block_t clearInspector = ^{
      if (inspector) {
          inspector = nil;
      }

      NSSetUncaughtExceptionHandler(NULL);
    };

    dispatch_block_t clear = ^{
      if (listenSource) {
          dispatch_source_cancel(listenSource);
          listenSource = nil;
      }

      clearInspector();
    };

    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationWillResignActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                  notify_post(NOTIFICATION("ApplicationWillResignActive"));
                }];

    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                  notify_post(NOTIFICATION("ApplicationDidBecomeActive"));
                }];

    TNSInspectorFrontendConnectedHandler connectionHandler = ^TNSInspectorProtocolHandler(
        TNSInspectorSendMessageBlock sendMessageToFrontend, NSError* error) {
      if (error) {
          if (listenSource) {
              clear();
          }

          NSLog(@"NativeScript debugger encountered %@.", error);
          return nil;
      }

      if (inspector) {
          return nil;
      }

      NSLog(@"NativeScript debugger attached.");

      inspector = [runtime attachInspectorWithHandler:sendMessageToFrontend];

      NSSetUncaughtExceptionHandler(&TNSInspectorUncaughtExceptionHandler);

      if (isWaitingForDebugger) {
          isWaitingForDebugger = NO;

          CFRunLoopRef runloop = CFRunLoopGetMain();
          CFRunLoopPerformBlock(
              runloop, (__bridge CFTypeRef)(NSRunLoopCommonModes), ^{
                // If we pause right away the debugger messages that are send
                // are not handled because the frontend is not yet initialized
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);

                [inspector pause];
              });
          CFRunLoopWakeUp(runloop);
      }

      NSArray* inspectorRunloopModes =
          @[ NSRunLoopCommonModes, TNSInspectorRunLoopMode ];
      return ^(NSString* message, NSError* error) {
        if (message) {
            CFRunLoopRef runloop = CFRunLoopGetMain();
            CFRunLoopPerformBlock(
                runloop, (__bridge CFTypeRef)(inspectorRunloopModes), ^{
                  [inspector dispatchMessage:message];
                });
            CFRunLoopWakeUp(runloop);
        } else {
            clearInspector();

            if (error) {
                NSLog(@"NativeScript debugger received %@. Disconnecting.",
                      error);
            } else {
                NSLog(@"NativeScript debugger detached.");
            }
        }
      };
    };

    TNSInspectorIoErrorHandler ioErrorHandler = ^(NSObject* dummy, NSError* error) {
      clearInspector();
      if (error) {
          NSLog(@"NativeScript debugger encountered %@.", error);
      }
    };

    int waitForDebuggerSubscription;
    notify_register_dispatch(
        NOTIFICATION("WaitForDebugger"), &waitForDebuggerSubscription,
        dispatch_get_main_queue(), ^(int token) {
          isWaitingForDebugger = YES;
          NSLog(@"NativeScript waiting for debugger.");

          CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopDefaultMode, ^{
            do {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
            } while (isWaitingForDebugger);
          });
          CFRunLoopWakeUp(CFRunLoopGetMain());
        });

    int attachRequestSubscription;
    notify_register_dispatch(
        NOTIFICATION("AttachRequest"), &attachRequestSubscription,
        dispatch_get_main_queue(), ^(int token) {
          if (listenSource) {
              clear();
          }

          listenSource = TNSCreateInspectorServer(connectionHandler, ioErrorHandler);
          notify_post(NOTIFICATION("ReadyForAttach"));
        });

    int attachAvailabilityQuerySubscription;
    notify_register_dispatch(NOTIFICATION("AttachAvailabilityQuery"),
                             &attachAvailabilityQuerySubscription,
                             dispatch_get_main_queue(), ^(int token) {
                               if (inspector) {
                                   notify_post(NOTIFICATION("AlreadyConnected"));
                               } else if (listenSource) {
                                   notify_post(NOTIFICATION("ReadyForAttach"));
                               } else {
                                   notify_post(NOTIFICATION("AttachAvailable"));
                               }
                             });

    notify_post(NOTIFICATION("AppLaunching"));

    for (int i = 1; i < argc; i++) {
        BOOL startListening = NO;
        BOOL shouldWaitForDebugger = NO;

        if (strcmp(argv[i], "--nativescript-debug-brk") == 0) {
            shouldWaitForDebugger = YES;
        } else if (strcmp(argv[i], "--nativescript-debug-start") == 0) {
            startListening = YES;
        }

        if (startListening || shouldWaitForDebugger) {
            notify_post(NOTIFICATION("AttachRequest"));
            if (shouldWaitForDebugger) {
                notify_post(NOTIFICATION("WaitForDebugger"));
            }

            break;
        }
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    notify_cancel(waitForDebuggerSubscription);
}
