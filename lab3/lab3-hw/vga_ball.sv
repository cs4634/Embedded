/*
 * Avalon memory-mapped peripheral that generates VGA
 *
 * Stephen A. Edwards
 * Columbia University
 *
 * Register map:
 * 
 * Address  Data[15:0]   Meaning
 *    0        X         Ball X coordinate (0-639)
 *    1        Y         Ball Y coordinate (0-479)
 *
 * Writes are staged, then applied only during blanking to avoid tearing.
 */

module vga_ball(
    input  logic        clk,
    input  logic        reset,
    input  logic [15:0] writedata,
    input  logic        write,
    input  logic        chipselect,
    input  logic [2:0]  address,

    output logic [7:0]  VGA_R, VGA_G, VGA_B,
    output logic        VGA_CLK, VGA_HS, VGA_VS,
                        VGA_BLANK_n,
    output logic        VGA_SYNC_n
);

   logic [10:0] hcount;
   logic [9:0]  vcount;

   localparam int Radius = 16;
   localparam int X_Max  = 639;
   localparam int Y_Max  = 479;

   logic [10:0] ball_x;
   logic [9:0]  ball_y;
   logic [10:0] pending_x;
   logic [9:0]  pending_y;
   logic        pending_valid;

   // fixed background color
   logic [7:0] background_r, background_g, background_b;

   vga_counters counters(.clk50(clk), .*);

   always_ff @(posedge clk) begin
      if (reset) begin
         background_r <= 8'h00;
         background_g <= 8'h00;
         background_b <= 8'h80;

         ball_x <= 11'd320;
         ball_y <= 10'd240;
         pending_x <= 11'd320;
         pending_y <= 10'd240;
         pending_valid <= 1'b0;
      end
      else begin
         if (chipselect && write) begin
            case (address)
               3'h0: begin
                  if (writedata[10:0] <= X_Max) begin
                     pending_x <= writedata[10:0];
                     pending_valid <= 1'b1;
                  end
               end

               3'h1: begin
                  if (writedata[9:0] <= Y_Max) begin
                     pending_y <= writedata[9:0];
                     pending_valid <= 1'b1;
                  end
               end
            endcase
         end

         // Apply new coordinates only during blanking to avoid visible tearing.
         if (!VGA_BLANK_n && pending_valid) begin
            ball_x <= pending_x;
            ball_y <= pending_y;
            pending_valid <= 1'b0;
         end
      end
   end

   logic signed [11:0] dx;
   logic signed [10:0] dy;

   assign dx = $signed({1'b0, hcount[10:1]}) - $signed({1'b0, ball_x});
   assign dy = $signed({1'b0, vcount[9:0]})  - $signed({1'b0, ball_y});

   always_comb begin
      {VGA_R, VGA_G, VGA_B} = {8'h00, 8'h00, 8'h00};

      if (VGA_BLANK_n) begin
         if (dx * dx + dy * dy <= Radius * Radius)
            {VGA_R, VGA_G, VGA_B} = {8'hff, 8'hff, 8'hff};
         else
            {VGA_R, VGA_G, VGA_B} = {background_r, background_g, background_b};
      end
   end

endmodule

module vga_counters(
 input logic         clk50, reset,
 output logic [10:0] hcount,  // hcount[10:1] is pixel column
 output logic [9:0]  vcount,  // vcount[9:0] is pixel row
 output logic        VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n);

/*
 * 640 X 480 VGA timing for a 50 MHz clock: one pixel every other cycle
 * 
 * HCOUNT 1599 0             1279       1599 0
 *             _______________              ________
 * ___________|    Video      |____________|  Video
 * 
 * 
 * |SYNC| BP |<-- HACTIVE -->|FP|SYNC| BP |<-- HACTIVE
 *       _______________________      _____________
 * |____|       VGA_HS          |____|
 */
   // Parameters for hcount
   parameter HACTIVE      = 11'd 1280,
             HFRONT_PORCH = 11'd 32,
             HSYNC        = 11'd 192,
             HBACK_PORCH  = 11'd 96,   
             HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC +
                            HBACK_PORCH; // 1600
   
   // Parameters for vcount
   parameter VACTIVE      = 10'd 480,
             VFRONT_PORCH = 10'd 10,
             VSYNC        = 10'd 2,
             VBACK_PORCH  = 10'd 33,
             VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC +
                            VBACK_PORCH; // 525

   logic endOfLine;
   
   always_ff @(posedge clk50 or posedge reset)
     if (reset)          hcount <= 0;
     else if (endOfLine) hcount <= 0;
     else                hcount <= hcount + 11'd 1;

   assign endOfLine = hcount == HTOTAL - 1;
       
   logic endOfField;
   
   always_ff @(posedge clk50 or posedge reset)
     if (reset)          vcount <= 0;
     else if (endOfLine)
       if (endOfField)   vcount <= 0;
       else              vcount <= vcount + 10'd 1;

   assign endOfField = vcount == VTOTAL - 1;

   // Horizontal sync: from 0x520 to 0x5DF (0x57F)
   // 101 0010 0000 to 101 1101 1111
   assign VGA_HS = !( (hcount[10:8] == 3'b101) &
                      !(hcount[7:5] == 3'b111));
   assign VGA_VS = !( vcount[9:1] == (VACTIVE + VFRONT_PORCH) / 2);

   assign VGA_SYNC_n = 1'b0; // For putting sync on the green signal; unused
   
   // Horizontal active: 0 to 1279     Vertical active: 0 to 479
   // 101 0000 0000  1280              01 1110 0000  480
   // 110 0011 1111  1599              10 0000 1100  524
   assign VGA_BLANK_n = !( hcount[10] & (hcount[9] | hcount[8]) ) &
                        !( vcount[9] | (vcount[8:5] == 4'b1111) );

   /* VGA_CLK is 25 MHz
    *             __    __    __
    * clk50    __|  |__|  |__|
    *        
    *             _____       __
    * hcount[0]__|     |_____|
    */
   assign VGA_CLK = hcount[0]; // 25 MHz clock: rising edge sensitive


   
endmodule
