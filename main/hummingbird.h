#pragma once

// Note: R"svg(...)svg" is a raw string literal. Everything between ( and ) is
// part of the string. This preserves the SVG content exactly as it is in the
// file.
static const char* hummingbird_svg = R"svg(
<svg width="75" height="75" viewBox="0 0 75 75" fill="none" xmlns="http://www.w3.org/2000/svg">
                
                <!-- Simple Flower for context (Bottom Left) -->
                <g class="flower">
                    <path d="M0 75 Q 10 50 20 45" stroke="#48BB78" stroke-width="1.5" fill="none"></path>
                    <!-- Petals -->
                    <circle cx="20" cy="45" r="4" fill="#F687B3"></circle>
                    <circle cx="16" cy="42" r="3" fill="#ED64A6"></circle>
                    <circle cx="24" cy="42" r="3" fill="#ED64A6"></circle>
                    <circle cx="20" cy="38" r="3" fill="#F687B3"></circle>
                    <circle cx="20" cy="45" r="1.5" fill="#F6E05E"></circle>
                </g>

                <!-- The Hummingbird Group -->
                <g class="bird-body">
                    
                    <!-- Back Wing (Darker) -->
                    <!-- Positioned behind body - Shifted X+6 -->
                    <path class="wing-back" d="M48 30 C 56 15, 66 10, 71 15 C 66 25, 56 32, 48 30 Z" fill="#2C7A7B" opacity="0.8"></path>

                    <!-- Tail -->
                    <path d="M55 45 L 65 55 L 70 50 Z" fill="#285E61"></path>

                    <!-- Body -->
                    <!-- Head to tail curve -->
                    <path d="M55 45 C 50 50, 40 45, 35 35 C 32 30, 32 25, 35 22 C 38 19, 45 20, 50 30 C 52 35, 55 40, 55 45 Z" fill="#38B2AC"></path>
                    
                    <!-- White Belly Patch -->
                    <path d="M40 38 Q 45 42 50 40 Q 48 45 42 45 Z" fill="#E6FFFA" opacity="0.5"></path>

                    <!-- Beak (Long and thin) -->
                    <path d="M35 25 L 15 30 L 35 27 Z" fill="#1A202C"></path>

                    <!-- Eye -->
                    <circle cx="37" cy="26" r="1.5" fill="black"></circle>
                    <circle cx="37.5" cy="25.5" r="0.5" fill="white"></circle>

                    <!-- Front Wing (Lighter/Brighter) -->
                    <!-- Shifted X+6 -->
                    <path class="wing-front" d="M44 32 C 51 10, 61 5, 68 10 C 64 20, 54 35, 44 32 Z" fill="#81E6D9" opacity="0.9"></path>
                    
                </g>

            </svg>
)svg";
