
#import "SDVirtualDisplayShim.h"

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#import <objc/message.h>

static id gVirtualDisplay = nil;

int32_t SDVirtualDisplayCreate(
    uint32_t width,
    uint32_t height,
    double refreshRate,
    uint32_t* displayID
) {
    if (width == 0 ||
        height == 0 ||
        refreshRate <= 0 ||
        displayID == nullptr) {
        return -1;
    }

    if (gVirtualDisplay != nil) {
        return -2;
    }

    @autoreleasepool {
        @try {
            Class descriptorClass =
                NSClassFromString(@"CGVirtualDisplayDescriptor");

            Class displayClass =
                NSClassFromString(@"CGVirtualDisplay");

            Class settingsClass =
                NSClassFromString(@"CGVirtualDisplaySettings");

            Class modeClass =
                NSClassFromString(@"CGVirtualDisplayMode");

            if (!descriptorClass ||
                !displayClass ||
                !settingsClass ||
                !modeClass) {
                return -3;
            }

            id descriptor =
                [[descriptorClass alloc] init];

            if (!descriptor) {
                return -4;
            }

            [descriptor setValue:@"SlimeDisplay"
                          forKey:@"name"];

            [descriptor setValue:@(width)
                          forKey:@"maxPixelsWide"];

            [descriptor setValue:@(height)
                          forKey:@"maxPixelsHigh"];

            [descriptor setValue:
                [NSValue valueWithSize:NSMakeSize(340, 190)]
                          forKey:@"sizeInMillimeters"];

            [descriptor setValue:@(0x3456)
                          forKey:@"vendorID"];

            [descriptor setValue:@(0x1234)
                          forKey:@"productID"];

            [descriptor setValue:@(1)
                          forKey:@"serialNum"];

            SEL initSelector =
                sel_registerName("initWithDescriptor:");

            if (![displayClass instancesRespondToSelector:initSelector]) {
                return -5;
            }

            id display =
                ((id (*)(id, SEL, id))objc_msgSend)(
                    [displayClass alloc],
                    initSelector,
                    descriptor
                );

            if (!display) {
                return -6;
            }

            SEL modeSelector =
                sel_registerName(
                    "initWithWidth:height:refreshRate:"
                );

            if (![modeClass instancesRespondToSelector:modeSelector]) {
                return -7;
            }

            id mode =
                ((id (*)(id, SEL, NSUInteger, NSUInteger, CGFloat))
                    objc_msgSend)(
                        [modeClass alloc],
                        modeSelector,
                        (NSUInteger)width,
                        (NSUInteger)height,
                        (CGFloat)refreshRate
                    );

            if (!mode) {
                return -8;
            }

            id settings =
                [[settingsClass alloc] init];

            [settings setValue:@[mode]
                        forKey:@"modes"];

            [settings setValue:@YES
                        forKey:@"hiDPI"];

            SEL applySelector =
                sel_registerName("applySettings:");

            if (![display respondsToSelector:applySelector]) {
                return -9;
            }

            BOOL applied =
                ((BOOL (*)(id, SEL, id))objc_msgSend)(
                    display,
                    applySelector,
                    settings
                );

            if (!applied) {
                return -10;
            }

            NSNumber* identifier =
                [display valueForKey:@"displayID"];

            if (!identifier) {
                return -11;
            }

            gVirtualDisplay = display;
            *displayID = identifier.unsignedIntValue;

            return 0;

        } @catch (NSException* exception) {
            NSLog(
                @"[SlimeDisplay] Virtual display creation failed: %@",
                exception
            );

            return -12;
        }
    }
}

void SDVirtualDisplayDestroy(void) {
    @autoreleasepool {
        gVirtualDisplay = nil;
    }
}
