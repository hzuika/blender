/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

#ifdef WITH_INPUT_IME
/* The Carbon API is still needed to check if the input source (or IME) is valid. */
#  import <Carbon/Carbon.h>
#endif

/* NSView subclass for drawing and handling input.
 *
 * COCOA_VIEW_BASE_CLASS will be either NSView or NSOpenGLView depending if
 * we use a Metal or OpenGL layer for drawing in the view. We use macros
 * to defined classes for each case, so we don't have to duplicate code as
 * Objective-C does not have multiple inheritance. */

// We need to subclass it in order to give Cocoa the feeling key events are trapped
@interface COCOA_VIEW_CLASS : COCOA_VIEW_BASE_CLASS <NSTextInputClient>
{
  GHOST_SystemCocoa *systemCocoa;
  GHOST_WindowCocoa *associatedWindow;

  bool composing;
  NSString *composing_text;

  bool immediate_draw;

#ifdef WITH_INPUT_IME
  bool use_ime;
  bool ime_is_composing;
  bool input_is_focused;
  NSRect ime_candidatewin_pos;
  GHOST_TEventImeData eventImeData;
#endif
}
- (void)setSystemAndWindowCocoa:(GHOST_SystemCocoa *)sysCocoa
                    windowCocoa:(GHOST_WindowCocoa *)winCocoa;

#ifdef WITH_INPUT_IME
- (void)beginIME:(GHOST_TInt32)x 
               y:(GHOST_TInt32)y 
               w:(GHOST_TInt32)w 
               h:(GHOST_TInt32)h
       completed:(bool)completed;

- (void)endIME;
#endif
@end

@implementation COCOA_VIEW_CLASS

- (void)setSystemAndWindowCocoa:(GHOST_SystemCocoa *)sysCocoa
                    windowCocoa:(GHOST_WindowCocoa *)winCocoa
{
  systemCocoa = sysCocoa;
  associatedWindow = winCocoa;

  composing = false;
  composing_text = nil;

  immediate_draw = false;

#ifdef WITH_INPUT_IME
  use_ime = false;
  ime_is_composing = false;
  input_is_focused = false;
  ime_candidatewin_pos = NSZeroRect;
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self
             selector:@selector(ImeDidChangeCallback:)
                 name:NSTextInputContextKeyboardSelectionDidChangeNotification
               object:nil];
#endif
}

- (BOOL)acceptsFirstResponder
{
  return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
  return YES;
}

// The trick to prevent Cocoa from complaining (beeping)
- (void)keyDown:(NSEvent *)event
{
#ifdef WITH_INPUT_IME
  /* Even if IME is enabled, when not composing, control characters 
   * (such as arrow, enter, delete) are handled by handleKeyEvent. */
  if (!use_ime || (!ime_is_composing && ![self eventCharacterIsAsciiDrawable:event])) {
#endif
    systemCocoa->handleKeyEvent(event);
#ifdef WITH_INPUT_IME
  }
#endif

  /* Start or continue composing? */
  if ([[event characters] length] == 0 || [[event charactersIgnoringModifiers] length] == 0 ||
      composing
#ifdef WITH_INPUT_IME
      || use_ime
#endif
  ) {
    composing = YES;

    // interpret event to call insertText
    [self interpretKeyEvents:[NSArray arrayWithObject: event]];  // calls insertText
    return;
  }

}

- (void)keyUp:(NSEvent *)event
{
  systemCocoa->handleKeyEvent(event);
}

- (void)flagsChanged:(NSEvent *)event
{
  systemCocoa->handleKeyEvent(event);
}

- (void)mouseDown:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)mouseUp:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)rightMouseDown:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)rightMouseUp:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)mouseMoved:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)mouseDragged:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)rightMouseDragged:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)scrollWheel:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)otherMouseDown:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)otherMouseUp:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)otherMouseDragged:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)magnifyWithEvent:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)smartMagnifyWithEvent:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)rotateWithEvent:(NSEvent *)event
{
  systemCocoa->handleMouseEvent(event);
}

- (void)tabletPoint:(NSEvent *)event
{
  systemCocoa->handleTabletEvent(event, [event type]);
}

- (void)tabletProximity:(NSEvent *)event
{
  systemCocoa->handleTabletEvent(event, [event type]);
}

- (BOOL)isOpaque
{
  return YES;
}

- (void)drawRect:(NSRect)rect
{
  if ([self inLiveResize]) {
    /* Don't redraw while in live resize */
  }
  else {
    [super drawRect:rect];
    systemCocoa->handleWindowEvent(GHOST_kEventWindowUpdate, associatedWindow);

    /* For some cases like entering fullscreen we need to redraw immediately
     * so our window does not show blank during the animation */
    if (associatedWindow->getImmediateDraw())
      systemCocoa->dispatchEvents();
  }
}

// Text input

- (void)composing_free
{
  composing = NO;

  if (composing_text) {
    [composing_text release];
    composing_text = nil;
  }
}

- (void)insertText:(id)chars replacementRange:(NSRange)replacementRange
{
  [self composing_free];
#ifdef WITH_INPUT_IME
  if (ime_is_composing) {
    size_t temp_buff_len;
    char *temp_buff = [self convertNSStringToChars:(NSString *)chars outlen_ptr:&temp_buff_len];
    [self setEventImeResultData:temp_buff result_len:temp_buff_len];

    [self processImeEvent:GHOST_kEventImeComposition];
    [self processImeEvent:GHOST_kEventImeCompositionEnd];

    ime_is_composing = false;
  }
#endif
}

- (void)setMarkedText:(id)chars selectedRange:(NSRange)range replacementRange:(NSRange)replacementRange
{
  [self composing_free];
  if ([chars length] == 0) {
#ifdef WITH_INPUT_IME
    /* When the last composition string is deleted */
    if (ime_is_composing) {
      [self setEventImeCompositionDeleteData];
      [self processImeEvent:GHOST_kEventImeComposition];
      [self processImeEvent:GHOST_kEventImeCompositionEnd];
      ime_is_composing = false;
    }
#endif
    return;
  }

  // start composing
  composing = YES;
  composing_text = [chars copy];

  // if empty, cancel
  if ([composing_text length] == 0)
    [self composing_free];

#ifdef WITH_INPUT_IME
  if (use_ime) {
    NSString *compositeString = (NSString *)[chars string];

    size_t temp_buff_len;
    char *temp_buff = [self convertNSStringToChars:compositeString outlen_ptr:&temp_buff_len];

    int cursor_position;
    int target_start;
    int target_end;
    [self getImeCursorPosAndTargetRange:compositeString selectedRange:range
                        outCursorPosPtr:&cursor_position
                      outTargetStartPtr:&target_start
                        outTargetEndPtr:&target_end];

    [self setEventImeCompositionData:temp_buff composite_len:temp_buff_len cursor_position:cursor_position
                        target_start:target_start target_end:target_end];
    if (ime_is_composing == false) {
      ime_is_composing = true;
      [self processImeEvent:GHOST_kEventImeCompositionStart];
    }
    [self processImeEvent:GHOST_kEventImeComposition];
  }
#endif
}

- (void)unmarkText
{
  [self composing_free];
}

- (BOOL)hasMarkedText
{
  return (composing) ? YES : NO;
}

- (void)doCommandBySelector:(SEL)selector
{
}

- (BOOL)isComposing
{
  return composing;
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
  return [[[NSAttributedString alloc] init] autorelease];
}

- (NSRange)markedRange
{
  unsigned int length = (composing_text) ? [composing_text length] : 0;

  if (composing)
    return NSMakeRange(0, length);

  return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
  unsigned int length = (composing_text) ? [composing_text length] : 0;
  return NSMakeRange(0, length);
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
#ifdef WITH_INPUT_IME
  if (use_ime){
    return ime_candidatewin_pos;
  }
#endif
  return NSZeroRect;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
  return NSNotFound;
}

- (NSArray *)validAttributesForMarkedText
{
  return [NSArray array];
}

#ifdef WITH_INPUT_IME
- (void)checkImeEnabled
{
  TISInputSourceRef currentKeyboardInputSource = TISCopyCurrentKeyboardInputSource();
  bool ime_is_enabled = !CFBooleanGetValue((CFBooleanRef)TISGetInputSourceProperty(currentKeyboardInputSource, kTISPropertyInputSourceIsASCIICapable));
  use_ime = (input_is_focused && ime_is_enabled);

  CFRelease(currentKeyboardInputSource);
}

- (void)ImeDidChangeCallback:(NSNotification*)notification 
{
  [self checkImeEnabled];
}

- (void)setImeCandidateWinPos:(GHOST_TInt32)x 
                            y:(GHOST_TInt32)y 
                            w:(GHOST_TInt32)w 
                            h:(GHOST_TInt32)h
{
  GHOST_TInt32 outX;
  GHOST_TInt32 outY;
  associatedWindow->clientToScreen(x, y, outX, outY);
  ime_candidatewin_pos = NSMakeRect((CGFloat)outX, (CGFloat)outY, (CGFloat)w, (CGFloat)h);
}

- (void)beginIME:(GHOST_TInt32)x 
               y:(GHOST_TInt32)y 
               w:(GHOST_TInt32)w 
               h:(GHOST_TInt32)h
       completed:(bool)completed
{

  input_is_focused = true;
  [self checkImeEnabled];
  [self setImeCandidateWinPos:x y:y w:w h:h];
}

- (void)endIME
{
  use_ime = false;
  ime_is_composing = false;
  input_is_focused = false;
  eventImeData.result_len = NULL;
  if (eventImeData.result) {
    free(eventImeData.result);
  }
  eventImeData.result = NULL;
  eventImeData.composite_len = NULL;
  if (eventImeData.composite) {
    free(eventImeData.composite);
  }
  eventImeData.composite = NULL;
  [self unmarkText];
  [[NSTextInputContext currentInputContext] discardMarkedText];
}

- (void)processImeEvent:(GHOST_TEventType)imeEventType
{
  GHOST_Event *event = new GHOST_EventIME(systemCocoa->getMilliSeconds(), imeEventType, associatedWindow, &eventImeData);
  systemCocoa->pushEvent(event);
}

- (void)setEventImeCompositionData:(char *)composite_buff
                     composite_len:(size_t)composite_len
                   cursor_position:(int)cursor_position
                      target_start:(int)target_start
                        target_end:(int)target_end
{
  eventImeData.result_len = NULL;
  eventImeData.result = NULL;
  eventImeData.composite_len = (GHOST_TUserDataPtr)composite_len;
  eventImeData.composite = (GHOST_TUserDataPtr)composite_buff;
  eventImeData.cursor_position = cursor_position;
  eventImeData.target_start = target_start;
  eventImeData.target_end = target_end;
}

- (void)setEventImeResultData:(char *)result_buff
                   result_len:(size_t)result_len
{
  eventImeData.result_len = (GHOST_TUserDataPtr)result_len;
  eventImeData.composite_len = NULL;
  eventImeData.result = (GHOST_TUserDataPtr)result_buff;
  eventImeData.composite = NULL;
  eventImeData.cursor_position = -1;
  eventImeData.target_start = -1;
  eventImeData.target_end = -1;
}

- (void)setEventImeCompositionDeleteData
{
  [self setEventImeResultData:NULL result_len:0];
}

- (char *)convertNSStringToChars:(NSString *)nsstring outlen_ptr:(size_t *)outlen_ptr
{
  const char* temp_buff = (char *) [nsstring UTF8String];
  size_t len = (*outlen_ptr) = strlen(temp_buff);
  char *outstr = (char *)malloc(len + 1);
  strncpy((char *)outstr, temp_buff, len);
  outstr[len] = '\0'; 
  return outstr;
}

/* The target string is equivalent to the string in selectedRange of setMarkedText. */
/* The cursor is displayed at the beginning of the target string. */
- (void)getImeCursorPosAndTargetRange:(NSString *)nsstring
                        selectedRange:(NSRange)range
                      outCursorPosPtr:(int *)cursor_position_ptr
                    outTargetStartPtr:(int *)target_start_ptr
                      outTargetEndPtr:(int *)target_end_ptr
{
    char *front_string = (char *) [[nsstring substringWithRange: NSMakeRange(0, range.location)] UTF8String];
    char *selected_string = (char *) [[nsstring substringWithRange: range] UTF8String];
    *cursor_position_ptr = strlen(front_string);
    *target_start_ptr = (*cursor_position_ptr);
    *target_end_ptr = (*target_start_ptr) + strlen(selected_string);
}

-(bool)eventCharacterIsAsciiDrawable:(NSEvent *)event
{
  int ascii = [[event charactersIgnoringModifiers] characterAtIndex:0];
  return (ascii < '~' && ascii > '!');
}

#endif /* WITH_INPUT_IME */

@end
